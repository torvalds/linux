//===-- RISCVInstructions.h -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_INSTRUCTION_RISCV_RISCVINSTRUCTION_H
#define LLDB_SOURCE_PLUGINS_INSTRUCTION_RISCV_RISCVINSTRUCTION_H

#include <cstdint>
#include <optional>
#include <variant>

#include "llvm/ADT/APFloat.h"

namespace lldb_private {

class EmulateInstructionRISCV;

struct Rd {
  uint32_t rd;
  bool Write(EmulateInstructionRISCV &emulator, uint64_t value);
  bool WriteAPFloat(EmulateInstructionRISCV &emulator, llvm::APFloat value);
};

struct Rs {
  uint32_t rs;
  std::optional<uint64_t> Read(EmulateInstructionRISCV &emulator);
  std::optional<int32_t> ReadI32(EmulateInstructionRISCV &emulator);
  std::optional<int64_t> ReadI64(EmulateInstructionRISCV &emulator);
  std::optional<uint32_t> ReadU32(EmulateInstructionRISCV &emulator);
  std::optional<llvm::APFloat> ReadAPFloat(EmulateInstructionRISCV &emulator,
                                           bool isDouble);
};

#define DERIVE_EQ(NAME)                                                        \
  bool operator==(const NAME &r) const {                                       \
    return std::memcmp(this, &r, sizeof(NAME)) == 0;                           \
  }

#define I_TYPE_INST(NAME)                                                      \
  struct NAME {                                                                \
    Rd rd;                                                                     \
    Rs rs1;                                                                    \
    uint32_t imm;                                                              \
    DERIVE_EQ(NAME);                                                           \
  }
#define S_TYPE_INST(NAME)                                                      \
  struct NAME {                                                                \
    Rs rs1;                                                                    \
    Rs rs2;                                                                    \
    uint32_t imm;                                                              \
    DERIVE_EQ(NAME);                                                           \
  }
#define U_TYPE_INST(NAME)                                                      \
  struct NAME {                                                                \
    Rd rd;                                                                     \
    uint32_t imm;                                                              \
    DERIVE_EQ(NAME);                                                           \
  }
/// The memory layout are the same in our code.
#define J_TYPE_INST(NAME) U_TYPE_INST(NAME)
#define R_TYPE_INST(NAME)                                                      \
  struct NAME {                                                                \
    Rd rd;                                                                     \
    Rs rs1;                                                                    \
    Rs rs2;                                                                    \
    DERIVE_EQ(NAME);                                                           \
  }
#define R_SHAMT_TYPE_INST(NAME)                                                \
  struct NAME {                                                                \
    Rd rd;                                                                     \
    Rs rs1;                                                                    \
    uint32_t shamt;                                                            \
    DERIVE_EQ(NAME);                                                           \
  }
#define R_RS1_TYPE_INST(NAME)                                                  \
  struct NAME {                                                                \
    Rd rd;                                                                     \
    Rs rs1;                                                                    \
    DERIVE_EQ(NAME);                                                           \
  }
#define R4_TYPE_INST(NAME)                                                     \
  struct NAME {                                                                \
    Rd rd;                                                                     \
    Rs rs1;                                                                    \
    Rs rs2;                                                                    \
    Rs rs3;                                                                    \
    int32_t rm;                                                                \
    DERIVE_EQ(NAME);                                                           \
  }
/// The `inst` fields are used for debugging.
#define INVALID_INST(NAME)                                                     \
  struct NAME {                                                                \
    uint32_t inst;                                                             \
    DERIVE_EQ(NAME);                                                           \
  }

// RV32I instructions (The base integer ISA)
struct B {
  Rs rs1;
  Rs rs2;
  uint32_t imm;
  uint32_t funct3;
  DERIVE_EQ(B);
};
U_TYPE_INST(LUI);
U_TYPE_INST(AUIPC);
J_TYPE_INST(JAL);
I_TYPE_INST(JALR);
I_TYPE_INST(LB);
I_TYPE_INST(LH);
I_TYPE_INST(LW);
I_TYPE_INST(LBU);
I_TYPE_INST(LHU);
S_TYPE_INST(SB);
S_TYPE_INST(SH);
S_TYPE_INST(SW);
I_TYPE_INST(ADDI);
I_TYPE_INST(SLTI);
I_TYPE_INST(SLTIU);
I_TYPE_INST(XORI);
I_TYPE_INST(ORI);
I_TYPE_INST(ANDI);
R_TYPE_INST(ADD);
R_TYPE_INST(SUB);
R_TYPE_INST(SLL);
R_TYPE_INST(SLT);
R_TYPE_INST(SLTU);
R_TYPE_INST(XOR);
R_TYPE_INST(SRL);
R_TYPE_INST(SRA);
R_TYPE_INST(OR);
R_TYPE_INST(AND);

// RV64I inst (The base integer ISA)
I_TYPE_INST(LWU);
I_TYPE_INST(LD);
S_TYPE_INST(SD);
R_SHAMT_TYPE_INST(SLLI);
R_SHAMT_TYPE_INST(SRLI);
R_SHAMT_TYPE_INST(SRAI);
I_TYPE_INST(ADDIW);
R_SHAMT_TYPE_INST(SLLIW);
R_SHAMT_TYPE_INST(SRLIW);
R_SHAMT_TYPE_INST(SRAIW);
R_TYPE_INST(ADDW);
R_TYPE_INST(SUBW);
R_TYPE_INST(SLLW);
R_TYPE_INST(SRLW);
R_TYPE_INST(SRAW);

// RV32M inst (The standard integer multiplication and division extension)
R_TYPE_INST(MUL);
R_TYPE_INST(MULH);
R_TYPE_INST(MULHSU);
R_TYPE_INST(MULHU);
R_TYPE_INST(DIV);
R_TYPE_INST(DIVU);
R_TYPE_INST(REM);
R_TYPE_INST(REMU);

// RV64M inst (The standard integer multiplication and division extension)
R_TYPE_INST(MULW);
R_TYPE_INST(DIVW);
R_TYPE_INST(DIVUW);
R_TYPE_INST(REMW);
R_TYPE_INST(REMUW);

// RV32A inst (The standard atomic instruction extension)
R_RS1_TYPE_INST(LR_W);
R_TYPE_INST(SC_W);
R_TYPE_INST(AMOSWAP_W);
R_TYPE_INST(AMOADD_W);
R_TYPE_INST(AMOXOR_W);
R_TYPE_INST(AMOAND_W);
R_TYPE_INST(AMOOR_W);
R_TYPE_INST(AMOMIN_W);
R_TYPE_INST(AMOMAX_W);
R_TYPE_INST(AMOMINU_W);
R_TYPE_INST(AMOMAXU_W);

// RV64A inst (The standard atomic instruction extension)
R_RS1_TYPE_INST(LR_D);
R_TYPE_INST(SC_D);
R_TYPE_INST(AMOSWAP_D);
R_TYPE_INST(AMOADD_D);
R_TYPE_INST(AMOXOR_D);
R_TYPE_INST(AMOAND_D);
R_TYPE_INST(AMOOR_D);
R_TYPE_INST(AMOMIN_D);
R_TYPE_INST(AMOMAX_D);
R_TYPE_INST(AMOMINU_D);
R_TYPE_INST(AMOMAXU_D);

// RV32F inst (The standard single-precision floating-point extension)
I_TYPE_INST(FLW);
S_TYPE_INST(FSW);
R4_TYPE_INST(FMADD_S);
R4_TYPE_INST(FMSUB_S);
R4_TYPE_INST(FNMADD_S);
R4_TYPE_INST(FNMSUB_S);
R_TYPE_INST(FADD_S);
R_TYPE_INST(FSUB_S);
R_TYPE_INST(FMUL_S);
R_TYPE_INST(FDIV_S);
I_TYPE_INST(FSQRT_S);
R_TYPE_INST(FSGNJ_S);
R_TYPE_INST(FSGNJN_S);
R_TYPE_INST(FSGNJX_S);
R_TYPE_INST(FMIN_S);
R_TYPE_INST(FMAX_S);
I_TYPE_INST(FCVT_W_S);
I_TYPE_INST(FCVT_WU_S);
I_TYPE_INST(FMV_X_W);
R_TYPE_INST(FEQ_S);
R_TYPE_INST(FLT_S);
R_TYPE_INST(FLE_S);
I_TYPE_INST(FCLASS_S);
I_TYPE_INST(FCVT_S_W);
I_TYPE_INST(FCVT_S_WU);
I_TYPE_INST(FMV_W_X);

// RV64F inst (The standard single-precision floating-point extension)
I_TYPE_INST(FCVT_L_S);
I_TYPE_INST(FCVT_LU_S);
I_TYPE_INST(FCVT_S_L);
I_TYPE_INST(FCVT_S_LU);

// RV32D inst (Extension for Double-Precision Floating-Point)
I_TYPE_INST(FLD);
S_TYPE_INST(FSD);
R4_TYPE_INST(FMADD_D);
R4_TYPE_INST(FMSUB_D);
R4_TYPE_INST(FNMSUB_D);
R4_TYPE_INST(FNMADD_D);
R_TYPE_INST(FADD_D);
R_TYPE_INST(FSUB_D);
R_TYPE_INST(FMUL_D);
R_TYPE_INST(FDIV_D);
I_TYPE_INST(FSQRT_D);
R_TYPE_INST(FSGNJ_D);
R_TYPE_INST(FSGNJN_D);
R_TYPE_INST(FSGNJX_D);
R_TYPE_INST(FMIN_D);
R_TYPE_INST(FMAX_D);
I_TYPE_INST(FCVT_S_D);
I_TYPE_INST(FCVT_D_S);
R_TYPE_INST(FEQ_D);
R_TYPE_INST(FLT_D);
R_TYPE_INST(FLE_D);
I_TYPE_INST(FCLASS_D);
I_TYPE_INST(FCVT_W_D);
I_TYPE_INST(FCVT_WU_D);
I_TYPE_INST(FCVT_D_W);
I_TYPE_INST(FCVT_D_WU);

// RV64D inst (Extension for Double-Precision Floating-Point)
I_TYPE_INST(FCVT_L_D);
I_TYPE_INST(FCVT_LU_D);
I_TYPE_INST(FMV_X_D);
I_TYPE_INST(FCVT_D_L);
I_TYPE_INST(FCVT_D_LU);
I_TYPE_INST(FMV_D_X);

/// Invalid and reserved instructions, the `inst` fields are used for debugging.
INVALID_INST(INVALID);
INVALID_INST(RESERVED);
INVALID_INST(EBREAK);
INVALID_INST(HINT);
INVALID_INST(NOP);

using RISCVInst = std::variant<
    LUI, AUIPC, JAL, JALR, B, LB, LH, LW, LBU, LHU, SB, SH, SW, ADDI, SLTI,
    SLTIU, XORI, ORI, ANDI, ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND,
    LWU, LD, SD, SLLI, SRLI, SRAI, ADDIW, SLLIW, SRLIW, SRAIW, ADDW, SUBW, SLLW,
    SRLW, SRAW, MUL, MULH, MULHSU, MULHU, DIV, DIVU, REM, REMU, MULW, DIVW,
    DIVUW, REMW, REMUW, LR_W, SC_W, AMOSWAP_W, AMOADD_W, AMOXOR_W, AMOAND_W,
    AMOOR_W, AMOMIN_W, AMOMAX_W, AMOMINU_W, AMOMAXU_W, LR_D, SC_D, AMOSWAP_D,
    AMOADD_D, AMOXOR_D, AMOAND_D, AMOOR_D, AMOMIN_D, AMOMAX_D, AMOMINU_D,
    AMOMAXU_D, FLW, FSW, FMADD_S, FMSUB_S, FNMADD_S, FNMSUB_S, FADD_S, FSUB_S,
    FMUL_S, FDIV_S, FSQRT_S, FSGNJ_S, FSGNJN_S, FSGNJX_S, FMIN_S, FMAX_S,
    FCVT_W_S, FCVT_WU_S, FMV_X_W, FEQ_S, FLT_S, FLE_S, FCLASS_S, FCVT_S_W,
    FCVT_S_WU, FMV_W_X, FCVT_L_S, FCVT_LU_S, FCVT_S_L, FCVT_S_LU, FLD, FSD,
    FMADD_D, FMSUB_D, FNMSUB_D, FNMADD_D, FADD_D, FSUB_D, FMUL_D, FDIV_D,
    FSQRT_D, FSGNJ_D, FSGNJN_D, FSGNJX_D, FMIN_D, FMAX_D, FCVT_S_D, FCVT_D_S,
    FEQ_D, FLT_D, FLE_D, FCLASS_D, FCVT_W_D, FCVT_WU_D, FCVT_D_W, FCVT_D_WU,
    FCVT_L_D, FCVT_LU_D, FMV_X_D, FCVT_D_L, FCVT_D_LU, FMV_D_X, INVALID, EBREAK,
    RESERVED, HINT, NOP>;

constexpr uint8_t RV32 = 1;
constexpr uint8_t RV64 = 2;
constexpr uint8_t RV128 = 4;

struct InstrPattern {
  const char *name;
  /// Bit mask to check the type of a instruction (B-Type, I-Type, J-Type, etc.)
  uint32_t type_mask;
  /// Characteristic value after bitwise-and with type_mask.
  uint32_t eigen;
  RISCVInst (*decode)(uint32_t inst);
  /// If not specified, the inst will be supported by all RV versions.
  uint8_t inst_type = RV32 | RV64 | RV128;
};

struct DecodeResult {
  RISCVInst decoded;
  uint32_t inst;
  bool is_rvc;
  InstrPattern pattern;
};

constexpr uint32_t DecodeRD(uint32_t inst) { return (inst & 0xF80) >> 7; }
constexpr uint32_t DecodeRS1(uint32_t inst) { return (inst & 0xF8000) >> 15; }
constexpr uint32_t DecodeRS2(uint32_t inst) { return (inst & 0x1F00000) >> 20; }
constexpr uint32_t DecodeRS3(uint32_t inst) {
  return (inst & 0xF0000000) >> 27;
}
constexpr uint32_t DecodeFunct3(uint32_t inst) { return (inst & 0x7000) >> 12; }
constexpr uint32_t DecodeFunct2(uint32_t inst) {
  return (inst & 0xE000000) >> 25;
}
constexpr uint32_t DecodeFunct7(uint32_t inst) {
  return (inst & 0xFE000000) >> 25;
}

constexpr int32_t DecodeRM(uint32_t inst) { return DecodeFunct3(inst); }

/// RISC-V spec: The upper bits of a valid NaN-boxed value must be all 1s.
constexpr uint64_t NanBoxing(uint64_t val) {
  return val | 0xFFFF'FFFF'0000'0000;
}
constexpr uint32_t NanUnBoxing(uint64_t val) {
  return val & (~0xFFFF'FFFF'0000'0000);
}

#undef R_TYPE_INST
#undef R_SHAMT_TYPE_INST
#undef R_RS1_TYPE_INST
#undef R4_TYPE_INST
#undef I_TYPE_INST
#undef S_TYPE_INST
#undef B_TYPE_INST
#undef U_TYPE_INST
#undef J_TYPE_INST
#undef INVALID_INST
#undef DERIVE_EQ

} // namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_INSTRUCTION_RISCV_RISCVINSTRUCTION_H
