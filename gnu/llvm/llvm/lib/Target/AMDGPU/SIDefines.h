//===-- SIDefines.h - SI Helper Macros ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
/// \file
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_SIDEFINES_H
#define LLVM_LIB_TARGET_AMDGPU_SIDEFINES_H

#include "llvm/MC/MCInstrDesc.h"

namespace llvm {

// This needs to be kept in sync with the field bits in SIRegisterClass.
enum SIRCFlags : uint8_t {
  RegTupleAlignUnitsWidth = 2,
  HasVGPRBit = RegTupleAlignUnitsWidth,
  HasAGPRBit,
  HasSGPRbit,

  HasVGPR = 1 << HasVGPRBit,
  HasAGPR = 1 << HasAGPRBit,
  HasSGPR = 1 << HasSGPRbit,

  RegTupleAlignUnitsMask = (1 << RegTupleAlignUnitsWidth) - 1,
  RegKindMask = (HasVGPR | HasAGPR | HasSGPR)
}; // enum SIRCFlagsr

namespace SIEncodingFamily {
// This must be kept in sync with the SIEncodingFamily class in SIInstrInfo.td
// and the columns of the getMCOpcodeGen table.
enum {
  SI = 0,
  VI = 1,
  SDWA = 2,
  SDWA9 = 3,
  GFX80 = 4,
  GFX9 = 5,
  GFX10 = 6,
  SDWA10 = 7,
  GFX90A = 8,
  GFX940 = 9,
  GFX11 = 10,
  GFX12 = 11,
};
}

namespace SIInstrFlags {
// This needs to be kept in sync with the field bits in InstSI.
enum : uint64_t {
  // Low bits - basic encoding information.
  SALU = 1 << 0,
  VALU = 1 << 1,

  // SALU instruction formats.
  SOP1 = 1 << 2,
  SOP2 = 1 << 3,
  SOPC = 1 << 4,
  SOPK = 1 << 5,
  SOPP = 1 << 6,

  // VALU instruction formats.
  VOP1 = 1 << 7,
  VOP2 = 1 << 8,
  VOPC = 1 << 9,

  // TODO: Should this be spilt into VOP3 a and b?
  VOP3 = 1 << 10,
  VOP3P = 1 << 12,

  VINTRP = 1 << 13,
  SDWA = 1 << 14,
  DPP = 1 << 15,
  TRANS = 1 << 16,

  // Memory instruction formats.
  MUBUF = 1 << 17,
  MTBUF = 1 << 18,
  SMRD = 1 << 19,
  MIMG = 1 << 20,
  VIMAGE = 1 << 21,
  VSAMPLE = 1 << 22,
  EXP = 1 << 23,
  FLAT = 1 << 24,
  DS = 1 << 25,

  // Combined SGPR/VGPR Spill bit
  // Logic to separate them out is done in isSGPRSpill and isVGPRSpill
  Spill = 1 << 26,

  // LDSDIR instruction format.
  LDSDIR = 1 << 28,

  // VINTERP instruction format.
  VINTERP = 1 << 29,

  // High bits - other information.
  VM_CNT = UINT64_C(1) << 32,
  EXP_CNT = UINT64_C(1) << 33,
  LGKM_CNT = UINT64_C(1) << 34,

  WQM = UINT64_C(1) << 35,
  DisableWQM = UINT64_C(1) << 36,
  Gather4 = UINT64_C(1) << 37,

  // Reserved, must be 0.
  Reserved0 = UINT64_C(1) << 38,

  SCALAR_STORE = UINT64_C(1) << 39,
  FIXED_SIZE = UINT64_C(1) << 40,

  // Reserved, must be 0.
  Reserved1 = UINT64_C(1) << 41,

  VOP3_OPSEL = UINT64_C(1) << 42,
  maybeAtomic = UINT64_C(1) << 43,
  renamedInGFX9 = UINT64_C(1) << 44,

  // Is a clamp on FP type.
  FPClamp = UINT64_C(1) << 45,

  // Is an integer clamp
  IntClamp = UINT64_C(1) << 46,

  // Clamps lo component of register.
  ClampLo = UINT64_C(1) << 47,

  // Clamps hi component of register.
  // ClampLo and ClampHi set for packed clamp.
  ClampHi = UINT64_C(1) << 48,

  // Is a packed VOP3P instruction.
  IsPacked = UINT64_C(1) << 49,

  // Is a D16 buffer instruction.
  D16Buf = UINT64_C(1) << 50,

  // FLAT instruction accesses FLAT_GLBL segment.
  FlatGlobal = UINT64_C(1) << 51,

  // Uses floating point double precision rounding mode
  FPDPRounding = UINT64_C(1) << 52,

  // Instruction is FP atomic.
  FPAtomic = UINT64_C(1) << 53,

  // Is a MFMA instruction.
  IsMAI = UINT64_C(1) << 54,

  // Is a DOT instruction.
  IsDOT = UINT64_C(1) << 55,

  // FLAT instruction accesses FLAT_SCRATCH segment.
  FlatScratch = UINT64_C(1) << 56,

  // Atomic without return.
  IsAtomicNoRet = UINT64_C(1) << 57,

  // Atomic with return.
  IsAtomicRet = UINT64_C(1) << 58,

  // Is a WMMA instruction.
  IsWMMA = UINT64_C(1) << 59,

  // Whether tied sources will be read.
  TiedSourceNotRead = UINT64_C(1) << 60,

  // Is never uniform.
  IsNeverUniform = UINT64_C(1) << 61,

  // ds_gws_* instructions.
  GWS = UINT64_C(1) << 62,

  // Is a SWMMAC instruction.
  IsSWMMAC = UINT64_C(1) << 63,
};

// v_cmp_class_* etc. use a 10-bit mask for what operation is checked.
// The result is true if any of these tests are true.
enum ClassFlags : unsigned {
  S_NAN = 1 << 0,        // Signaling NaN
  Q_NAN = 1 << 1,        // Quiet NaN
  N_INFINITY = 1 << 2,   // Negative infinity
  N_NORMAL = 1 << 3,     // Negative normal
  N_SUBNORMAL = 1 << 4,  // Negative subnormal
  N_ZERO = 1 << 5,       // Negative zero
  P_ZERO = 1 << 6,       // Positive zero
  P_SUBNORMAL = 1 << 7,  // Positive subnormal
  P_NORMAL = 1 << 8,     // Positive normal
  P_INFINITY = 1 << 9    // Positive infinity
};
}

namespace AMDGPU {
enum OperandType : unsigned {
  /// Operands with register or 32-bit immediate
  OPERAND_REG_IMM_INT32 = MCOI::OPERAND_FIRST_TARGET,
  OPERAND_REG_IMM_INT64,
  OPERAND_REG_IMM_INT16,
  OPERAND_REG_IMM_FP32,
  OPERAND_REG_IMM_FP64,
  OPERAND_REG_IMM_BF16,
  OPERAND_REG_IMM_FP16,
  OPERAND_REG_IMM_BF16_DEFERRED,
  OPERAND_REG_IMM_FP16_DEFERRED,
  OPERAND_REG_IMM_FP32_DEFERRED,
  OPERAND_REG_IMM_V2BF16,
  OPERAND_REG_IMM_V2FP16,
  OPERAND_REG_IMM_V2INT16,
  OPERAND_REG_IMM_V2INT32,
  OPERAND_REG_IMM_V2FP32,

  /// Operands with register or inline constant
  OPERAND_REG_INLINE_C_INT16,
  OPERAND_REG_INLINE_C_INT32,
  OPERAND_REG_INLINE_C_INT64,
  OPERAND_REG_INLINE_C_BF16,
  OPERAND_REG_INLINE_C_FP16,
  OPERAND_REG_INLINE_C_FP32,
  OPERAND_REG_INLINE_C_FP64,
  OPERAND_REG_INLINE_C_V2INT16,
  OPERAND_REG_INLINE_C_V2BF16,
  OPERAND_REG_INLINE_C_V2FP16,
  OPERAND_REG_INLINE_C_V2INT32,
  OPERAND_REG_INLINE_C_V2FP32,

  // Operand for split barrier inline constant
  OPERAND_INLINE_SPLIT_BARRIER_INT32,

  /// Operand with 32-bit immediate that uses the constant bus.
  OPERAND_KIMM32,
  OPERAND_KIMM16,

  /// Operands with an AccVGPR register or inline constant
  OPERAND_REG_INLINE_AC_INT16,
  OPERAND_REG_INLINE_AC_INT32,
  OPERAND_REG_INLINE_AC_BF16,
  OPERAND_REG_INLINE_AC_FP16,
  OPERAND_REG_INLINE_AC_FP32,
  OPERAND_REG_INLINE_AC_FP64,
  OPERAND_REG_INLINE_AC_V2INT16,
  OPERAND_REG_INLINE_AC_V2BF16,
  OPERAND_REG_INLINE_AC_V2FP16,
  OPERAND_REG_INLINE_AC_V2INT32,
  OPERAND_REG_INLINE_AC_V2FP32,

  // Operand for source modifiers for VOP instructions
  OPERAND_INPUT_MODS,

  // Operand for SDWA instructions
  OPERAND_SDWA_VOPC_DST,

  OPERAND_REG_IMM_FIRST = OPERAND_REG_IMM_INT32,
  OPERAND_REG_IMM_LAST = OPERAND_REG_IMM_V2FP32,

  OPERAND_REG_INLINE_C_FIRST = OPERAND_REG_INLINE_C_INT16,
  OPERAND_REG_INLINE_C_LAST = OPERAND_REG_INLINE_AC_V2FP32,

  OPERAND_REG_INLINE_AC_FIRST = OPERAND_REG_INLINE_AC_INT16,
  OPERAND_REG_INLINE_AC_LAST = OPERAND_REG_INLINE_AC_V2FP32,

  OPERAND_SRC_FIRST = OPERAND_REG_IMM_INT32,
  OPERAND_SRC_LAST = OPERAND_REG_INLINE_C_LAST,

  OPERAND_KIMM_FIRST = OPERAND_KIMM32,
  OPERAND_KIMM_LAST = OPERAND_KIMM16

};

// Should be in sync with the OperandSemantics defined in SIRegisterInfo.td
enum OperandSemantics : unsigned {
  INT = 0,
  FP16 = 1,
  BF16 = 2,
  FP32 = 3,
  FP64 = 4,
};
}

// Input operand modifiers bit-masks
// NEG and SEXT share same bit-mask because they can't be set simultaneously.
namespace SISrcMods {
  enum : unsigned {
   NONE = 0,
   NEG = 1 << 0,   // Floating-point negate modifier
   ABS = 1 << 1,   // Floating-point absolute modifier
   SEXT = 1 << 0,  // Integer sign-extend modifier
   NEG_HI = ABS,   // Floating-point negate high packed component modifier.
   OP_SEL_0 = 1 << 2,
   OP_SEL_1 = 1 << 3,
   DST_OP_SEL = 1 << 3 // VOP3 dst op_sel (share mask with OP_SEL_1)
  };
}

namespace SIOutMods {
  enum : unsigned {
    NONE = 0,
    MUL2 = 1,
    MUL4 = 2,
    DIV2 = 3
  };
}

namespace AMDGPU {
namespace VGPRIndexMode {

enum Id : unsigned { // id of symbolic names
  ID_SRC0 = 0,
  ID_SRC1,
  ID_SRC2,
  ID_DST,

  ID_MIN = ID_SRC0,
  ID_MAX = ID_DST
};

enum EncBits : unsigned {
  OFF = 0,
  SRC0_ENABLE = 1 << ID_SRC0,
  SRC1_ENABLE = 1 << ID_SRC1,
  SRC2_ENABLE = 1 << ID_SRC2,
  DST_ENABLE = 1 << ID_DST,
  ENABLE_MASK = SRC0_ENABLE | SRC1_ENABLE | SRC2_ENABLE | DST_ENABLE,
  UNDEF = 0xFFFF
};

} // namespace VGPRIndexMode
} // namespace AMDGPU

namespace AMDGPUAsmVariants {
  enum : unsigned {
    DEFAULT = 0,
    VOP3 = 1,
    SDWA = 2,
    SDWA9 = 3,
    DPP = 4,
    VOP3_DPP = 5
  };
} // namespace AMDGPUAsmVariants

namespace AMDGPU {
namespace EncValues { // Encoding values of enum9/8/7 operands

enum : unsigned {
  SGPR_MIN = 0,
  SGPR_MAX_SI = 101,
  SGPR_MAX_GFX10 = 105,
  TTMP_VI_MIN = 112,
  TTMP_VI_MAX = 123,
  TTMP_GFX9PLUS_MIN = 108,
  TTMP_GFX9PLUS_MAX = 123,
  INLINE_INTEGER_C_MIN = 128,
  INLINE_INTEGER_C_POSITIVE_MAX = 192, // 64
  INLINE_INTEGER_C_MAX = 208,
  INLINE_FLOATING_C_MIN = 240,
  INLINE_FLOATING_C_MAX = 248,
  LITERAL_CONST = 255,
  VGPR_MIN = 256,
  VGPR_MAX = 511,
  IS_VGPR = 256, // Indicates VGPR or AGPR
};

} // namespace EncValues

// Register codes as defined in the TableGen's HWEncoding field.
namespace HWEncoding {
enum : unsigned {
  REG_IDX_MASK = 0xff,
  IS_VGPR_OR_AGPR = 1 << 8,
  IS_HI = 1 << 9, // High 16-bit register.
};
} // namespace HWEncoding

namespace CPol {

enum CPol {
  GLC = 1,
  SLC = 2,
  DLC = 4,
  SCC = 16,
  SC0 = GLC,
  SC1 = SCC,
  NT = SLC,
  ALL_pregfx12 = GLC | SLC | DLC | SCC,
  SWZ_pregfx12 = 8,

  // Below are GFX12+ cache policy bits

  // Temporal hint
  TH = 0x7,      // All TH bits
  TH_RT = 0,     // regular
  TH_NT = 1,     // non-temporal
  TH_HT = 2,     // high-temporal
  TH_LU = 3,     // last use
  TH_RT_WB = 3,  // regular (CU, SE), high-temporal with write-back (MALL)
  TH_NT_RT = 4,  // non-temporal (CU, SE), regular (MALL)
  TH_RT_NT = 5,  // regular (CU, SE), non-temporal (MALL)
  TH_NT_HT = 6,  // non-temporal (CU, SE), high-temporal (MALL)
  TH_NT_WB = 7,  // non-temporal (CU, SE), high-temporal with write-back (MALL)
  TH_BYPASS = 3, // only to be used with scope = 3

  TH_RESERVED = 7, // unused value for load insts

  // Bits of TH for atomics
  TH_ATOMIC_RETURN = GLC, // Returning vs non-returning
  TH_ATOMIC_NT = SLC,     // Non-temporal vs regular
  TH_ATOMIC_CASCADE = 4,  // Cascading vs regular

  // Scope
  SCOPE = 0x3 << 3, // All Scope bits
  SCOPE_CU = 0 << 3,
  SCOPE_SE = 1 << 3,
  SCOPE_DEV = 2 << 3,
  SCOPE_SYS = 3 << 3,

  SWZ = 1 << 6, // Swizzle bit

  ALL = TH | SCOPE,

  // Helper bits
  TH_TYPE_LOAD = 1 << 7,    // TH_LOAD policy
  TH_TYPE_STORE = 1 << 8,   // TH_STORE policy
  TH_TYPE_ATOMIC = 1 << 9,  // TH_ATOMIC policy
  TH_REAL_BYPASS = 1 << 10, // is TH=3 bypass policy or not

  // Volatile (used to preserve/signal operation volatility for buffer
  // operations not a real instruction bit)
  VOLATILE = 1 << 31,
};

} // namespace CPol

namespace SendMsg { // Encoding of SIMM16 used in s_sendmsg* insns.

enum Id { // Message ID, width(4) [3:0].
  ID_INTERRUPT = 1,

  ID_GS_PreGFX11 = 2,      // replaced in GFX11
  ID_GS_DONE_PreGFX11 = 3, // replaced in GFX11

  ID_HS_TESSFACTOR_GFX11Plus = 2, // reused in GFX11
  ID_DEALLOC_VGPRS_GFX11Plus = 3, // reused in GFX11

  ID_SAVEWAVE = 4,           // added in GFX8, removed in GFX11
  ID_STALL_WAVE_GEN = 5,     // added in GFX9, removed in GFX12
  ID_HALT_WAVES = 6,         // added in GFX9, removed in GFX12
  ID_ORDERED_PS_DONE = 7,    // added in GFX9, removed in GFX11
  ID_EARLY_PRIM_DEALLOC = 8, // added in GFX9, removed in GFX10
  ID_GS_ALLOC_REQ = 9,       // added in GFX9
  ID_GET_DOORBELL = 10,      // added in GFX9, removed in GFX11
  ID_GET_DDID = 11,          // added in GFX10, removed in GFX11
  ID_SYSMSG = 15,

  ID_RTN_GET_DOORBELL = 128,
  ID_RTN_GET_DDID = 129,
  ID_RTN_GET_TMA = 130,
  ID_RTN_GET_REALTIME = 131,
  ID_RTN_SAVE_WAVE = 132,
  ID_RTN_GET_TBA = 133,
  ID_RTN_GET_TBA_TO_PC = 134,
  ID_RTN_GET_SE_AID_ID = 135,

  ID_MASK_PreGFX11_ = 0xF,
  ID_MASK_GFX11Plus_ = 0xFF
};

enum Op { // Both GS and SYS operation IDs.
  OP_SHIFT_ = 4,
  OP_NONE_ = 0,
  // Bits used for operation encoding
  OP_WIDTH_ = 3,
  OP_MASK_ = (((1 << OP_WIDTH_) - 1) << OP_SHIFT_),
  // GS operations are encoded in bits 5:4
  OP_GS_NOP = 0,
  OP_GS_CUT = 1,
  OP_GS_EMIT = 2,
  OP_GS_EMIT_CUT = 3,
  OP_GS_FIRST_ = OP_GS_NOP,
  // SYS operations are encoded in bits 6:4
  OP_SYS_ECC_ERR_INTERRUPT = 1,
  OP_SYS_REG_RD = 2,
  OP_SYS_HOST_TRAP_ACK = 3,
  OP_SYS_TTRACE_PC = 4,
  OP_SYS_FIRST_ = OP_SYS_ECC_ERR_INTERRUPT,
};

enum StreamId : unsigned { // Stream ID, (2) [9:8].
  STREAM_ID_NONE_ = 0,
  STREAM_ID_DEFAULT_ = 0,
  STREAM_ID_LAST_ = 4,
  STREAM_ID_FIRST_ = STREAM_ID_DEFAULT_,
  STREAM_ID_SHIFT_ = 8,
  STREAM_ID_WIDTH_=  2,
  STREAM_ID_MASK_ = (((1 << STREAM_ID_WIDTH_) - 1) << STREAM_ID_SHIFT_)
};

} // namespace SendMsg

namespace Hwreg { // Encoding of SIMM16 used in s_setreg/getreg* insns.

enum Id { // HwRegCode, (6) [5:0]
  ID_MODE = 1,
  ID_STATUS = 2,
  ID_TRAPSTS = 3,
  ID_HW_ID = 4,
  ID_GPR_ALLOC = 5,
  ID_LDS_ALLOC = 6,
  ID_IB_STS = 7,
  ID_PERF_SNAPSHOT_DATA_gfx12 = 10,
  ID_PERF_SNAPSHOT_PC_LO_gfx12 = 11,
  ID_PERF_SNAPSHOT_PC_HI_gfx12 = 12,
  ID_MEM_BASES = 15,
  ID_TBA_LO = 16,
  ID_TBA_HI = 17,
  ID_TMA_LO = 18,
  ID_TMA_HI = 19,
  ID_FLAT_SCR_LO = 20,
  ID_FLAT_SCR_HI = 21,
  ID_XNACK_MASK = 22,
  ID_HW_ID1 = 23,
  ID_HW_ID2 = 24,
  ID_POPS_PACKER = 25,
  ID_PERF_SNAPSHOT_DATA_gfx11 = 27,
  ID_SHADER_CYCLES = 29,
  ID_SHADER_CYCLES_HI = 30,
  ID_DVGPR_ALLOC_LO = 31,
  ID_DVGPR_ALLOC_HI = 32,

  // Register numbers reused in GFX11
  ID_PERF_SNAPSHOT_PC_LO_gfx11 = 18,
  ID_PERF_SNAPSHOT_PC_HI_gfx11 = 19,

  // Register numbers reused in GFX12+
  ID_STATE_PRIV = 4,
  ID_PERF_SNAPSHOT_DATA1 = 15,
  ID_PERF_SNAPSHOT_DATA2 = 16,
  ID_EXCP_FLAG_PRIV = 17,
  ID_EXCP_FLAG_USER = 18,
  ID_TRAP_CTRL = 19,

  // GFX940 specific registers
  ID_XCC_ID = 20,
  ID_SQ_PERF_SNAPSHOT_DATA = 21,
  ID_SQ_PERF_SNAPSHOT_DATA1 = 22,
  ID_SQ_PERF_SNAPSHOT_PC_LO = 23,
  ID_SQ_PERF_SNAPSHOT_PC_HI = 24,
};

enum Offset : unsigned { // Offset, (5) [10:6]
  OFFSET_MEM_VIOL = 8,
};

enum ModeRegisterMasks : uint32_t {
  FP_ROUND_MASK = 0xf << 0,  // Bits 0..3
  FP_DENORM_MASK = 0xf << 4, // Bits 4..7
  DX10_CLAMP_MASK = 1 << 8,
  IEEE_MODE_MASK = 1 << 9,
  LOD_CLAMP_MASK = 1 << 10,
  DEBUG_MASK = 1 << 11,

  // EXCP_EN fields.
  EXCP_EN_INVALID_MASK = 1 << 12,
  EXCP_EN_INPUT_DENORMAL_MASK = 1 << 13,
  EXCP_EN_FLOAT_DIV0_MASK = 1 << 14,
  EXCP_EN_OVERFLOW_MASK = 1 << 15,
  EXCP_EN_UNDERFLOW_MASK = 1 << 16,
  EXCP_EN_INEXACT_MASK = 1 << 17,
  EXCP_EN_INT_DIV0_MASK = 1 << 18,

  GPR_IDX_EN_MASK = 1 << 27,
  VSKIP_MASK = 1 << 28,
  CSP_MASK = 0x7u << 29 // Bits 29..31
};

} // namespace Hwreg

namespace MTBUFFormat {

enum DataFormat : int64_t {
  DFMT_INVALID = 0,
  DFMT_8,
  DFMT_16,
  DFMT_8_8,
  DFMT_32,
  DFMT_16_16,
  DFMT_10_11_11,
  DFMT_11_11_10,
  DFMT_10_10_10_2,
  DFMT_2_10_10_10,
  DFMT_8_8_8_8,
  DFMT_32_32,
  DFMT_16_16_16_16,
  DFMT_32_32_32,
  DFMT_32_32_32_32,
  DFMT_RESERVED_15,

  DFMT_MIN = DFMT_INVALID,
  DFMT_MAX = DFMT_RESERVED_15,

  DFMT_UNDEF = -1,
  DFMT_DEFAULT = DFMT_8,

  DFMT_SHIFT = 0,
  DFMT_MASK = 0xF
};

enum NumFormat : int64_t {
  NFMT_UNORM = 0,
  NFMT_SNORM,
  NFMT_USCALED,
  NFMT_SSCALED,
  NFMT_UINT,
  NFMT_SINT,
  NFMT_RESERVED_6,                    // VI and GFX9
  NFMT_SNORM_OGL = NFMT_RESERVED_6,   // SI and CI only
  NFMT_FLOAT,

  NFMT_MIN = NFMT_UNORM,
  NFMT_MAX = NFMT_FLOAT,

  NFMT_UNDEF = -1,
  NFMT_DEFAULT = NFMT_UNORM,

  NFMT_SHIFT = 4,
  NFMT_MASK = 7
};

enum MergedFormat : int64_t {
  DFMT_NFMT_UNDEF = -1,
  DFMT_NFMT_DEFAULT = ((DFMT_DEFAULT & DFMT_MASK) << DFMT_SHIFT) |
                      ((NFMT_DEFAULT & NFMT_MASK) << NFMT_SHIFT),


  DFMT_NFMT_MASK = (DFMT_MASK << DFMT_SHIFT) | (NFMT_MASK << NFMT_SHIFT),

  DFMT_NFMT_MAX = DFMT_NFMT_MASK
};

enum UnifiedFormatCommon : int64_t {
  UFMT_MAX = 127,
  UFMT_UNDEF = -1,
  UFMT_DEFAULT = 1
};

} // namespace MTBUFFormat

namespace UfmtGFX10 {
enum UnifiedFormat : int64_t {
  UFMT_INVALID = 0,

  UFMT_8_UNORM,
  UFMT_8_SNORM,
  UFMT_8_USCALED,
  UFMT_8_SSCALED,
  UFMT_8_UINT,
  UFMT_8_SINT,

  UFMT_16_UNORM,
  UFMT_16_SNORM,
  UFMT_16_USCALED,
  UFMT_16_SSCALED,
  UFMT_16_UINT,
  UFMT_16_SINT,
  UFMT_16_FLOAT,

  UFMT_8_8_UNORM,
  UFMT_8_8_SNORM,
  UFMT_8_8_USCALED,
  UFMT_8_8_SSCALED,
  UFMT_8_8_UINT,
  UFMT_8_8_SINT,

  UFMT_32_UINT,
  UFMT_32_SINT,
  UFMT_32_FLOAT,

  UFMT_16_16_UNORM,
  UFMT_16_16_SNORM,
  UFMT_16_16_USCALED,
  UFMT_16_16_SSCALED,
  UFMT_16_16_UINT,
  UFMT_16_16_SINT,
  UFMT_16_16_FLOAT,

  UFMT_10_11_11_UNORM,
  UFMT_10_11_11_SNORM,
  UFMT_10_11_11_USCALED,
  UFMT_10_11_11_SSCALED,
  UFMT_10_11_11_UINT,
  UFMT_10_11_11_SINT,
  UFMT_10_11_11_FLOAT,

  UFMT_11_11_10_UNORM,
  UFMT_11_11_10_SNORM,
  UFMT_11_11_10_USCALED,
  UFMT_11_11_10_SSCALED,
  UFMT_11_11_10_UINT,
  UFMT_11_11_10_SINT,
  UFMT_11_11_10_FLOAT,

  UFMT_10_10_10_2_UNORM,
  UFMT_10_10_10_2_SNORM,
  UFMT_10_10_10_2_USCALED,
  UFMT_10_10_10_2_SSCALED,
  UFMT_10_10_10_2_UINT,
  UFMT_10_10_10_2_SINT,

  UFMT_2_10_10_10_UNORM,
  UFMT_2_10_10_10_SNORM,
  UFMT_2_10_10_10_USCALED,
  UFMT_2_10_10_10_SSCALED,
  UFMT_2_10_10_10_UINT,
  UFMT_2_10_10_10_SINT,

  UFMT_8_8_8_8_UNORM,
  UFMT_8_8_8_8_SNORM,
  UFMT_8_8_8_8_USCALED,
  UFMT_8_8_8_8_SSCALED,
  UFMT_8_8_8_8_UINT,
  UFMT_8_8_8_8_SINT,

  UFMT_32_32_UINT,
  UFMT_32_32_SINT,
  UFMT_32_32_FLOAT,

  UFMT_16_16_16_16_UNORM,
  UFMT_16_16_16_16_SNORM,
  UFMT_16_16_16_16_USCALED,
  UFMT_16_16_16_16_SSCALED,
  UFMT_16_16_16_16_UINT,
  UFMT_16_16_16_16_SINT,
  UFMT_16_16_16_16_FLOAT,

  UFMT_32_32_32_UINT,
  UFMT_32_32_32_SINT,
  UFMT_32_32_32_FLOAT,
  UFMT_32_32_32_32_UINT,
  UFMT_32_32_32_32_SINT,
  UFMT_32_32_32_32_FLOAT,

  UFMT_FIRST = UFMT_INVALID,
  UFMT_LAST = UFMT_32_32_32_32_FLOAT,
};

} // namespace UfmtGFX10

namespace UfmtGFX11 {
enum UnifiedFormat : int64_t {
  UFMT_INVALID = 0,

  UFMT_8_UNORM,
  UFMT_8_SNORM,
  UFMT_8_USCALED,
  UFMT_8_SSCALED,
  UFMT_8_UINT,
  UFMT_8_SINT,

  UFMT_16_UNORM,
  UFMT_16_SNORM,
  UFMT_16_USCALED,
  UFMT_16_SSCALED,
  UFMT_16_UINT,
  UFMT_16_SINT,
  UFMT_16_FLOAT,

  UFMT_8_8_UNORM,
  UFMT_8_8_SNORM,
  UFMT_8_8_USCALED,
  UFMT_8_8_SSCALED,
  UFMT_8_8_UINT,
  UFMT_8_8_SINT,

  UFMT_32_UINT,
  UFMT_32_SINT,
  UFMT_32_FLOAT,

  UFMT_16_16_UNORM,
  UFMT_16_16_SNORM,
  UFMT_16_16_USCALED,
  UFMT_16_16_SSCALED,
  UFMT_16_16_UINT,
  UFMT_16_16_SINT,
  UFMT_16_16_FLOAT,

  UFMT_10_11_11_FLOAT,

  UFMT_11_11_10_FLOAT,

  UFMT_10_10_10_2_UNORM,
  UFMT_10_10_10_2_SNORM,
  UFMT_10_10_10_2_UINT,
  UFMT_10_10_10_2_SINT,

  UFMT_2_10_10_10_UNORM,
  UFMT_2_10_10_10_SNORM,
  UFMT_2_10_10_10_USCALED,
  UFMT_2_10_10_10_SSCALED,
  UFMT_2_10_10_10_UINT,
  UFMT_2_10_10_10_SINT,

  UFMT_8_8_8_8_UNORM,
  UFMT_8_8_8_8_SNORM,
  UFMT_8_8_8_8_USCALED,
  UFMT_8_8_8_8_SSCALED,
  UFMT_8_8_8_8_UINT,
  UFMT_8_8_8_8_SINT,

  UFMT_32_32_UINT,
  UFMT_32_32_SINT,
  UFMT_32_32_FLOAT,

  UFMT_16_16_16_16_UNORM,
  UFMT_16_16_16_16_SNORM,
  UFMT_16_16_16_16_USCALED,
  UFMT_16_16_16_16_SSCALED,
  UFMT_16_16_16_16_UINT,
  UFMT_16_16_16_16_SINT,
  UFMT_16_16_16_16_FLOAT,

  UFMT_32_32_32_UINT,
  UFMT_32_32_32_SINT,
  UFMT_32_32_32_FLOAT,
  UFMT_32_32_32_32_UINT,
  UFMT_32_32_32_32_SINT,
  UFMT_32_32_32_32_FLOAT,

  UFMT_FIRST = UFMT_INVALID,
  UFMT_LAST = UFMT_32_32_32_32_FLOAT,
};

} // namespace UfmtGFX11

namespace Swizzle { // Encoding of swizzle macro used in ds_swizzle_b32.

enum Id : unsigned { // id of symbolic names
  ID_QUAD_PERM = 0,
  ID_BITMASK_PERM,
  ID_SWAP,
  ID_REVERSE,
  ID_BROADCAST
};

enum EncBits : unsigned {

  // swizzle mode encodings

  QUAD_PERM_ENC         = 0x8000,
  QUAD_PERM_ENC_MASK    = 0xFF00,

  BITMASK_PERM_ENC      = 0x0000,
  BITMASK_PERM_ENC_MASK = 0x8000,

  // QUAD_PERM encodings

  LANE_MASK             = 0x3,
  LANE_MAX              = LANE_MASK,
  LANE_SHIFT            = 2,
  LANE_NUM              = 4,

  // BITMASK_PERM encodings

  BITMASK_MASK          = 0x1F,
  BITMASK_MAX           = BITMASK_MASK,
  BITMASK_WIDTH         = 5,

  BITMASK_AND_SHIFT     = 0,
  BITMASK_OR_SHIFT      = 5,
  BITMASK_XOR_SHIFT     = 10
};

} // namespace Swizzle

namespace SDWA {

enum SdwaSel : unsigned {
  BYTE_0 = 0,
  BYTE_1 = 1,
  BYTE_2 = 2,
  BYTE_3 = 3,
  WORD_0 = 4,
  WORD_1 = 5,
  DWORD = 6,
};

enum DstUnused : unsigned {
  UNUSED_PAD = 0,
  UNUSED_SEXT = 1,
  UNUSED_PRESERVE = 2,
};

enum SDWA9EncValues : unsigned {
  SRC_SGPR_MASK = 0x100,
  SRC_VGPR_MASK = 0xFF,
  VOPC_DST_VCC_MASK = 0x80,
  VOPC_DST_SGPR_MASK = 0x7F,

  SRC_VGPR_MIN = 0,
  SRC_VGPR_MAX = 255,
  SRC_SGPR_MIN = 256,
  SRC_SGPR_MAX_SI = 357,
  SRC_SGPR_MAX_GFX10 = 361,
  SRC_TTMP_MIN = 364,
  SRC_TTMP_MAX = 379,
};

} // namespace SDWA

namespace DPP {

// clang-format off
enum DppCtrl : unsigned {
  QUAD_PERM_FIRST   = 0,
  QUAD_PERM_ID      = 0xE4, // identity permutation
  QUAD_PERM_LAST    = 0xFF,
  DPP_UNUSED1       = 0x100,
  ROW_SHL0          = 0x100,
  ROW_SHL_FIRST     = 0x101,
  ROW_SHL_LAST      = 0x10F,
  DPP_UNUSED2       = 0x110,
  ROW_SHR0          = 0x110,
  ROW_SHR_FIRST     = 0x111,
  ROW_SHR_LAST      = 0x11F,
  DPP_UNUSED3       = 0x120,
  ROW_ROR0          = 0x120,
  ROW_ROR_FIRST     = 0x121,
  ROW_ROR_LAST      = 0x12F,
  WAVE_SHL1         = 0x130,
  DPP_UNUSED4_FIRST = 0x131,
  DPP_UNUSED4_LAST  = 0x133,
  WAVE_ROL1         = 0x134,
  DPP_UNUSED5_FIRST = 0x135,
  DPP_UNUSED5_LAST  = 0x137,
  WAVE_SHR1         = 0x138,
  DPP_UNUSED6_FIRST = 0x139,
  DPP_UNUSED6_LAST  = 0x13B,
  WAVE_ROR1         = 0x13C,
  DPP_UNUSED7_FIRST = 0x13D,
  DPP_UNUSED7_LAST  = 0x13F,
  ROW_MIRROR        = 0x140,
  ROW_HALF_MIRROR   = 0x141,
  BCAST15           = 0x142,
  BCAST31           = 0x143,
  DPP_UNUSED8_FIRST = 0x144,
  DPP_UNUSED8_LAST  = 0x14F,
  ROW_NEWBCAST_FIRST= 0x150,
  ROW_NEWBCAST_LAST = 0x15F,
  ROW_SHARE0        = 0x150,
  ROW_SHARE_FIRST   = 0x150,
  ROW_SHARE_LAST    = 0x15F,
  ROW_XMASK0        = 0x160,
  ROW_XMASK_FIRST   = 0x160,
  ROW_XMASK_LAST    = 0x16F,
  DPP_LAST          = ROW_XMASK_LAST
};
// clang-format on

enum DppFiMode {
  DPP_FI_0  = 0,
  DPP_FI_1  = 1,
  DPP8_FI_0 = 0xE9,
  DPP8_FI_1 = 0xEA,
};

} // namespace DPP

namespace Exp {

enum Target : unsigned {
  ET_MRT0 = 0,
  ET_MRT7 = 7,
  ET_MRTZ = 8,
  ET_NULL = 9,             // Pre-GFX11
  ET_POS0 = 12,
  ET_POS3 = 15,
  ET_POS4 = 16,            // GFX10+
  ET_POS_LAST = ET_POS4,   // Highest pos used on any subtarget
  ET_PRIM = 20,            // GFX10+
  ET_DUAL_SRC_BLEND0 = 21, // GFX11+
  ET_DUAL_SRC_BLEND1 = 22, // GFX11+
  ET_PARAM0 = 32,          // Pre-GFX11
  ET_PARAM31 = 63,         // Pre-GFX11

  ET_NULL_MAX_IDX = 0,
  ET_MRTZ_MAX_IDX = 0,
  ET_PRIM_MAX_IDX = 0,
  ET_MRT_MAX_IDX = 7,
  ET_POS_MAX_IDX = 4,
  ET_DUAL_SRC_BLEND_MAX_IDX = 1,
  ET_PARAM_MAX_IDX = 31,

  ET_INVALID = 255,
};

} // namespace Exp

namespace VOP3PEncoding {

enum OpSel : uint64_t {
  OP_SEL_HI_0 = UINT64_C(1) << 59,
  OP_SEL_HI_1 = UINT64_C(1) << 60,
  OP_SEL_HI_2 = UINT64_C(1) << 14,
};

} // namespace VOP3PEncoding

namespace ImplicitArg {
// Implicit kernel argument offset for code object version 5.
enum Offset_COV5 : unsigned {
  HOSTCALL_PTR_OFFSET = 80,
  MULTIGRID_SYNC_ARG_OFFSET = 88,
  HEAP_PTR_OFFSET = 96,

  DEFAULT_QUEUE_OFFSET = 104,
  COMPLETION_ACTION_OFFSET = 112,

  PRIVATE_BASE_OFFSET = 192,
  SHARED_BASE_OFFSET = 196,
  QUEUE_PTR_OFFSET = 200,
};

} // namespace ImplicitArg

namespace VirtRegFlag {
// Virtual register flags used for various target specific handlings during
// codegen.
enum Register_Flag : uint8_t {
  // Register operand in a whole-wave mode operation.
  WWM_REG = 1 << 0,
};

} // namespace VirtRegFlag

} // namespace AMDGPU

namespace AMDGPU {
namespace Barrier {
enum Type { TRAP = -2, WORKGROUP = -1 };
} // namespace Barrier
} // namespace AMDGPU

// clang-format off

#define R_00B028_SPI_SHADER_PGM_RSRC1_PS                                0x00B028
#define   S_00B028_VGPRS(x)                                           (((x) & 0x3F) << 0)
#define   S_00B028_SGPRS(x)                                           (((x) & 0x0F) << 6)
#define   S_00B028_MEM_ORDERED(x)                                     (((x) & 0x1) << 25)
#define   G_00B028_MEM_ORDERED(x)                                     (((x) >> 25) & 0x1)
#define   C_00B028_MEM_ORDERED                                        0xFDFFFFFF

#define R_00B02C_SPI_SHADER_PGM_RSRC2_PS                                0x00B02C
#define   S_00B02C_EXTRA_LDS_SIZE(x)                                  (((x) & 0xFF) << 8)
#define R_00B128_SPI_SHADER_PGM_RSRC1_VS                                0x00B128
#define   S_00B128_MEM_ORDERED(x)                                     (((x) & 0x1) << 27)
#define   G_00B128_MEM_ORDERED(x)                                     (((x) >> 27) & 0x1)
#define   C_00B128_MEM_ORDERED                                        0xF7FFFFFF

#define R_00B228_SPI_SHADER_PGM_RSRC1_GS                                0x00B228
#define   S_00B228_WGP_MODE(x)                                        (((x) & 0x1) << 27)
#define   G_00B228_WGP_MODE(x)                                        (((x) >> 27) & 0x1)
#define   C_00B228_WGP_MODE                                           0xF7FFFFFF
#define   S_00B228_MEM_ORDERED(x)                                     (((x) & 0x1) << 25)
#define   G_00B228_MEM_ORDERED(x)                                     (((x) >> 25) & 0x1)
#define   C_00B228_MEM_ORDERED                                        0xFDFFFFFF

#define R_00B328_SPI_SHADER_PGM_RSRC1_ES                                0x00B328
#define R_00B428_SPI_SHADER_PGM_RSRC1_HS                                0x00B428
#define   S_00B428_WGP_MODE(x)                                        (((x) & 0x1) << 26)
#define   G_00B428_WGP_MODE(x)                                        (((x) >> 26) & 0x1)
#define   C_00B428_WGP_MODE                                           0xFBFFFFFF
#define   S_00B428_MEM_ORDERED(x)                                     (((x) & 0x1) << 24)
#define   G_00B428_MEM_ORDERED(x)                                     (((x) >> 24) & 0x1)
#define   C_00B428_MEM_ORDERED                                        0xFEFFFFFF

#define R_00B528_SPI_SHADER_PGM_RSRC1_LS                                0x00B528

#define R_00B84C_COMPUTE_PGM_RSRC2                                      0x00B84C
#define   S_00B84C_SCRATCH_EN(x)                                      (((x) & 0x1) << 0)
#define   G_00B84C_SCRATCH_EN(x)                                      (((x) >> 0) & 0x1)
#define   C_00B84C_SCRATCH_EN                                         0xFFFFFFFE
#define   S_00B84C_USER_SGPR(x)                                       (((x) & 0x1F) << 1)
#define   G_00B84C_USER_SGPR(x)                                       (((x) >> 1) & 0x1F)
#define   C_00B84C_USER_SGPR                                          0xFFFFFFC1
#define   S_00B84C_TRAP_HANDLER(x)                                    (((x) & 0x1) << 6)
#define   G_00B84C_TRAP_HANDLER(x)                                    (((x) >> 6) & 0x1)
#define   C_00B84C_TRAP_HANDLER                                       0xFFFFFFBF
#define   S_00B84C_TGID_X_EN(x)                                       (((x) & 0x1) << 7)
#define   G_00B84C_TGID_X_EN(x)                                       (((x) >> 7) & 0x1)
#define   C_00B84C_TGID_X_EN                                          0xFFFFFF7F
#define   S_00B84C_TGID_Y_EN(x)                                       (((x) & 0x1) << 8)
#define   G_00B84C_TGID_Y_EN(x)                                       (((x) >> 8) & 0x1)
#define   C_00B84C_TGID_Y_EN                                          0xFFFFFEFF
#define   S_00B84C_TGID_Z_EN(x)                                       (((x) & 0x1) << 9)
#define   G_00B84C_TGID_Z_EN(x)                                       (((x) >> 9) & 0x1)
#define   C_00B84C_TGID_Z_EN                                          0xFFFFFDFF
#define   S_00B84C_TG_SIZE_EN(x)                                      (((x) & 0x1) << 10)
#define   G_00B84C_TG_SIZE_EN(x)                                      (((x) >> 10) & 0x1)
#define   C_00B84C_TG_SIZE_EN                                         0xFFFFFBFF
#define   S_00B84C_TIDIG_COMP_CNT(x)                                  (((x) & 0x03) << 11)
#define   G_00B84C_TIDIG_COMP_CNT(x)                                  (((x) >> 11) & 0x03)
#define   C_00B84C_TIDIG_COMP_CNT                                     0xFFFFE7FF
/* CIK */
#define   S_00B84C_EXCP_EN_MSB(x)                                     (((x) & 0x03) << 13)
#define   G_00B84C_EXCP_EN_MSB(x)                                     (((x) >> 13) & 0x03)
#define   C_00B84C_EXCP_EN_MSB                                        0xFFFF9FFF
/*     */
#define   S_00B84C_LDS_SIZE(x)                                        (((x) & 0x1FF) << 15)
#define   G_00B84C_LDS_SIZE(x)                                        (((x) >> 15) & 0x1FF)
#define   C_00B84C_LDS_SIZE                                           0xFF007FFF
#define   S_00B84C_EXCP_EN(x)                                         (((x) & 0x7F) << 24)
#define   G_00B84C_EXCP_EN(x)                                         (((x) >> 24) & 0x7F)
#define   C_00B84C_EXCP_EN                                            0x80FFFFFF

#define R_0286CC_SPI_PS_INPUT_ENA                                       0x0286CC
#define R_0286D0_SPI_PS_INPUT_ADDR                                      0x0286D0

#define R_00B848_COMPUTE_PGM_RSRC1                                      0x00B848
#define   S_00B848_VGPRS(x)                                           (((x) & 0x3F) << 0)
#define   G_00B848_VGPRS(x)                                           (((x) >> 0) & 0x3F)
#define   C_00B848_VGPRS                                              0xFFFFFFC0
#define   S_00B848_SGPRS(x)                                           (((x) & 0x0F) << 6)
#define   G_00B848_SGPRS(x)                                           (((x) >> 6) & 0x0F)
#define   C_00B848_SGPRS                                              0xFFFFFC3F
#define   S_00B848_PRIORITY(x)                                        (((x) & 0x03) << 10)
#define   G_00B848_PRIORITY(x)                                        (((x) >> 10) & 0x03)
#define   C_00B848_PRIORITY                                           0xFFFFF3FF
#define   S_00B848_FLOAT_MODE(x)                                      (((x) & 0xFF) << 12)
#define   G_00B848_FLOAT_MODE(x)                                      (((x) >> 12) & 0xFF)
#define   C_00B848_FLOAT_MODE                                         0xFFF00FFF
#define   S_00B848_PRIV(x)                                            (((x) & 0x1) << 20)
#define   G_00B848_PRIV(x)                                            (((x) >> 20) & 0x1)
#define   C_00B848_PRIV                                               0xFFEFFFFF
#define   S_00B848_DX10_CLAMP(x)                                      (((x) & 0x1) << 21)
#define   G_00B848_DX10_CLAMP(x)                                      (((x) >> 21) & 0x1)
#define   C_00B848_DX10_CLAMP                                         0xFFDFFFFF
#define   S_00B848_RR_WG_MODE(x)                                      (((x) & 0x1) << 21)
#define   G_00B848_RR_WG_MODE(x)                                      (((x) >> 21) & 0x1)
#define   C_00B848_RR_WG_MODE                                         0xFFDFFFFF
#define   S_00B848_DEBUG_MODE(x)                                      (((x) & 0x1) << 22)
#define   G_00B848_DEBUG_MODE(x)                                      (((x) >> 22) & 0x1)
#define   C_00B848_DEBUG_MODE                                         0xFFBFFFFF
#define   S_00B848_IEEE_MODE(x)                                       (((x) & 0x1) << 23)
#define   G_00B848_IEEE_MODE(x)                                       (((x) >> 23) & 0x1)
#define   C_00B848_IEEE_MODE                                          0xFF7FFFFF
#define   S_00B848_WGP_MODE(x)                                        (((x) & 0x1) << 29)
#define   G_00B848_WGP_MODE(x)                                        (((x) >> 29) & 0x1)
#define   C_00B848_WGP_MODE                                           0xDFFFFFFF
#define   S_00B848_MEM_ORDERED(x)                                     (((x) & 0x1) << 30)
#define   G_00B848_MEM_ORDERED(x)                                     (((x) >> 30) & 0x1)
#define   C_00B848_MEM_ORDERED                                        0xBFFFFFFF
#define   S_00B848_FWD_PROGRESS(x)                                    (((x) & 0x1) << 31)
#define   G_00B848_FWD_PROGRESS(x)                                    (((x) >> 31) & 0x1)
#define   C_00B848_FWD_PROGRESS                                       0x7FFFFFFF

// Helpers for setting FLOAT_MODE
#define FP_ROUND_ROUND_TO_NEAREST 0
#define FP_ROUND_ROUND_TO_INF 1
#define FP_ROUND_ROUND_TO_NEGINF 2
#define FP_ROUND_ROUND_TO_ZERO 3

// Bits 3:0 control rounding mode. 1:0 control single precision, 3:2 double
// precision.
#define FP_ROUND_MODE_SP(x) ((x) & 0x3)
#define FP_ROUND_MODE_DP(x) (((x) & 0x3) << 2)

#define FP_DENORM_FLUSH_IN_FLUSH_OUT 0
#define FP_DENORM_FLUSH_OUT 1
#define FP_DENORM_FLUSH_IN 2
#define FP_DENORM_FLUSH_NONE 3


// Bits 7:4 control denormal handling. 5:4 control single precision, 6:7 double
// precision.
#define FP_DENORM_MODE_SP(x) (((x) & 0x3) << 4)
#define FP_DENORM_MODE_DP(x) (((x) & 0x3) << 6)

#define R_00B860_COMPUTE_TMPRING_SIZE                                   0x00B860
#define   S_00B860_WAVESIZE_PreGFX11(x)                               (((x) & 0x1FFF) << 12)
#define   S_00B860_WAVESIZE_GFX11(x)                                  (((x) & 0x7FFF) << 12)
#define   S_00B860_WAVESIZE_GFX12Plus(x)                              (((x) & 0x3FFFF) << 12)

#define R_0286E8_SPI_TMPRING_SIZE                                       0x0286E8
#define   S_0286E8_WAVESIZE_PreGFX11(x)                               (((x) & 0x1FFF) << 12)
#define   S_0286E8_WAVESIZE_GFX11(x)                                  (((x) & 0x7FFF) << 12)
#define   S_0286E8_WAVESIZE_GFX12Plus(x)                              (((x) & 0x3FFFF) << 12)

#define R_028B54_VGT_SHADER_STAGES_EN                                 0x028B54
#define   S_028B54_HS_W32_EN(x)                                       (((x) & 0x1) << 21)
#define   S_028B54_GS_W32_EN(x)                                       (((x) & 0x1) << 22)
#define   S_028B54_VS_W32_EN(x)                                       (((x) & 0x1) << 23)
#define R_0286D8_SPI_PS_IN_CONTROL                                    0x0286D8
#define   S_0286D8_PS_W32_EN(x)                                       (((x) & 0x1) << 15)
#define R_00B800_COMPUTE_DISPATCH_INITIATOR                           0x00B800
#define   S_00B800_CS_W32_EN(x)                                       (((x) & 0x1) << 15)

#define R_SPILLED_SGPRS         0x4
#define R_SPILLED_VGPRS         0x8

// clang-format on

} // End namespace llvm

#endif
