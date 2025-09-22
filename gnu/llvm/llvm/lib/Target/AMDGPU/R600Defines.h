//===-- R600Defines.h - R600 Helper Macros ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
/// \file
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600DEFINES_H
#define LLVM_LIB_TARGET_AMDGPU_R600DEFINES_H

// Operand Flags
#define MO_FLAG_CLAMP (1 << 0)
#define MO_FLAG_NEG   (1 << 1)
#define MO_FLAG_ABS   (1 << 2)
#define MO_FLAG_MASK  (1 << 3)
#define MO_FLAG_PUSH  (1 << 4)
#define MO_FLAG_NOT_LAST  (1 << 5)
#define MO_FLAG_LAST  (1 << 6)
#define NUM_MO_FLAGS 7

/// Helper for getting the operand index for the instruction flags
/// operand.
#define GET_FLAG_OPERAND_IDX(Flags) (((Flags) >> 7) & 0x3)

namespace R600_InstFlag {
  enum TIF {
    TRANS_ONLY = (1 << 0),
    TEX = (1 << 1),
    REDUCTION = (1 << 2),
    FC = (1 << 3),
    TRIG = (1 << 4),
    OP3 = (1 << 5),
    VECTOR = (1 << 6),
    //FlagOperand bits 7, 8
    NATIVE_OPERANDS = (1 << 9),
    OP1 = (1 << 10),
    OP2 = (1 << 11),
    VTX_INST  = (1 << 12),
    TEX_INST = (1 << 13),
    ALU_INST = (1 << 14),
    LDS_1A = (1 << 15),
    LDS_1A1D = (1 << 16),
    IS_EXPORT = (1 << 17),
    LDS_1A2D = (1 << 18)
  };
}

#define HAS_NATIVE_OPERANDS(Flags) ((Flags) & R600_InstFlag::NATIVE_OPERANDS)

/// Defines for extracting register information from register encoding
#define HW_REG_MASK 0x1ff
#define HW_CHAN_SHIFT 9

#define GET_REG_CHAN(reg) ((reg) >> HW_CHAN_SHIFT)
#define GET_REG_INDEX(reg) ((reg) & HW_REG_MASK)

#define IS_VTX(desc) ((desc).TSFlags & R600_InstFlag::VTX_INST)
#define IS_TEX(desc) ((desc).TSFlags & R600_InstFlag::TEX_INST)

namespace OpName {

  enum VecOps {
    UPDATE_EXEC_MASK_X,
    UPDATE_PREDICATE_X,
    WRITE_X,
    OMOD_X,
    DST_REL_X,
    CLAMP_X,
    SRC0_X,
    SRC0_NEG_X,
    SRC0_REL_X,
    SRC0_ABS_X,
    SRC0_SEL_X,
    SRC1_X,
    SRC1_NEG_X,
    SRC1_REL_X,
    SRC1_ABS_X,
    SRC1_SEL_X,
    PRED_SEL_X,
    UPDATE_EXEC_MASK_Y,
    UPDATE_PREDICATE_Y,
    WRITE_Y,
    OMOD_Y,
    DST_REL_Y,
    CLAMP_Y,
    SRC0_Y,
    SRC0_NEG_Y,
    SRC0_REL_Y,
    SRC0_ABS_Y,
    SRC0_SEL_Y,
    SRC1_Y,
    SRC1_NEG_Y,
    SRC1_REL_Y,
    SRC1_ABS_Y,
    SRC1_SEL_Y,
    PRED_SEL_Y,
    UPDATE_EXEC_MASK_Z,
    UPDATE_PREDICATE_Z,
    WRITE_Z,
    OMOD_Z,
    DST_REL_Z,
    CLAMP_Z,
    SRC0_Z,
    SRC0_NEG_Z,
    SRC0_REL_Z,
    SRC0_ABS_Z,
    SRC0_SEL_Z,
    SRC1_Z,
    SRC1_NEG_Z,
    SRC1_REL_Z,
    SRC1_ABS_Z,
    SRC1_SEL_Z,
    PRED_SEL_Z,
    UPDATE_EXEC_MASK_W,
    UPDATE_PREDICATE_W,
    WRITE_W,
    OMOD_W,
    DST_REL_W,
    CLAMP_W,
    SRC0_W,
    SRC0_NEG_W,
    SRC0_REL_W,
    SRC0_ABS_W,
    SRC0_SEL_W,
    SRC1_W,
    SRC1_NEG_W,
    SRC1_REL_W,
    SRC1_ABS_W,
    SRC1_SEL_W,
    PRED_SEL_W,
    IMM_0,
    IMM_1,
    VEC_COUNT
 };

}

//===----------------------------------------------------------------------===//
// Config register definitions
//===----------------------------------------------------------------------===//

#define R_02880C_DB_SHADER_CONTROL                    0x02880C
#define   S_02880C_KILL_ENABLE(x)                      (((x) & 0x1) << 6)

// These fields are the same for all shader types and families.
#define   S_NUM_GPRS(x)                         (((x) & 0xFF) << 0)
#define   S_STACK_SIZE(x)                       (((x) & 0xFF) << 8)
//===----------------------------------------------------------------------===//
// R600, R700 Registers
//===----------------------------------------------------------------------===//

#define R_028850_SQ_PGM_RESOURCES_PS                 0x028850
#define R_028868_SQ_PGM_RESOURCES_VS                 0x028868

//===----------------------------------------------------------------------===//
// Evergreen, Northern Islands Registers
//===----------------------------------------------------------------------===//

#define R_028844_SQ_PGM_RESOURCES_PS                 0x028844
#define R_028860_SQ_PGM_RESOURCES_VS                 0x028860
#define R_028878_SQ_PGM_RESOURCES_GS                 0x028878
#define R_0288D4_SQ_PGM_RESOURCES_LS                 0x0288d4

#define R_0288E8_SQ_LDS_ALLOC                        0x0288E8

#endif
