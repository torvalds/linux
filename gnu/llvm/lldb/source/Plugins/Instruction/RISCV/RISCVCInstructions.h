//===-- RISCVCInstructions.h ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_INSTRUCTION_RISCV_RISCVCINSTRUCTION_H
#define LLDB_SOURCE_PLUGINS_INSTRUCTION_RISCV_RISCVCINSTRUCTION_H

#include <cstdint>
#include <variant>

#include "Plugins/Process/Utility/lldb-riscv-register-enums.h"
#include "RISCVInstructions.h"

namespace lldb_private {

/// Unified RISC-V C register encoding.
struct RxC {
  uint32_t rd;
  bool shift = true;
  operator int() { return rd; }
  operator Rd() { return Rd{rd + (shift ? 8 : 0)}; }
  operator Rs() { return Rs{rd + (shift ? 8 : 0)}; }
};

// decode register for RVC
constexpr RxC DecodeCR_RD(uint32_t inst) { return RxC{DecodeRD(inst), false}; }
constexpr RxC DecodeCI_RD(uint32_t inst) { return RxC{DecodeRD(inst), false}; }
constexpr RxC DecodeCR_RS1(uint32_t inst) { return RxC{DecodeRD(inst), false}; }
constexpr RxC DecodeCI_RS1(uint32_t inst) { return RxC{DecodeRD(inst), false}; }
constexpr RxC DecodeCR_RS2(uint32_t inst) {
  return RxC{(inst & 0x7C) >> 2, false};
}

constexpr RxC DecodeCIW_RD(uint32_t inst) { return RxC{(inst & 0x1C) >> 2}; }
constexpr RxC DecodeCL_RD(uint32_t inst) { return RxC{DecodeCIW_RD(inst)}; }
constexpr RxC DecodeCA_RD(uint32_t inst) { return RxC{(inst & 0x380) >> 7}; }
constexpr RxC DecodeCB_RD(uint32_t inst) { return RxC{DecodeCA_RD(inst)}; }

constexpr RxC DecodeCL_RS1(uint32_t inst) { return RxC{DecodeCA_RD(inst)}; }
constexpr RxC DecodeCS_RS1(uint32_t inst) { return RxC{DecodeCA_RD(inst)}; }
constexpr RxC DecodeCA_RS1(uint32_t inst) { return RxC{DecodeCA_RD(inst)}; }
constexpr RxC DecodeCB_RS1(uint32_t inst) { return RxC{DecodeCA_RD(inst)}; }

constexpr RxC DecodeCSS_RS2(uint32_t inst) { return DecodeCR_RS2(inst); }
constexpr RxC DecodeCS_RS2(uint32_t inst) { return RxC{DecodeCIW_RD(inst)}; }
constexpr RxC DecodeCA_RS2(uint32_t inst) { return RxC{DecodeCIW_RD(inst)}; }

RISCVInst DecodeC_LWSP(uint32_t inst) {
  auto rd = DecodeCI_RD(inst);
  uint16_t offset = ((inst << 4) & 0xc0)    // offset[7:6]
                    | ((inst >> 7) & 0x20)  // offset[5]
                    | ((inst >> 2) & 0x1c); // offset[4:2]
  if (rd == 0)
    return RESERVED{inst};
  return LW{rd, Rs{gpr_sp_riscv}, uint32_t(offset)};
}

RISCVInst DecodeC_LDSP(uint32_t inst) {
  auto rd = DecodeCI_RD(inst);
  uint16_t offset = ((inst << 4) & 0x1c0)   // offset[8:6]
                    | ((inst >> 7) & 0x20)  // offset[5]
                    | ((inst >> 2) & 0x18); // offset[4:3]
  if (rd == 0)
    return RESERVED{inst};
  return LD{rd, Rs{gpr_sp_riscv}, uint32_t(offset)};
}

RISCVInst DecodeC_SWSP(uint32_t inst) {
  uint16_t offset = ((inst >> 1) & 0xc0)    // offset[7:6]
                    | ((inst >> 7) & 0x3c); // offset[5:2]
  return SW{Rs{gpr_sp_riscv}, DecodeCSS_RS2(inst), uint32_t(offset)};
}

RISCVInst DecodeC_SDSP(uint32_t inst) {
  uint16_t offset = ((inst >> 1) & 0x1c0)   // offset[8:6]
                    | ((inst >> 7) & 0x38); // offset[5:3]
  return SD{Rs{gpr_sp_riscv}, DecodeCSS_RS2(inst), uint32_t(offset)};
}

RISCVInst DecodeC_LW(uint32_t inst) {
  uint16_t offset = ((inst << 1) & 0x40)   // imm[6]
                    | ((inst >> 7) & 0x38) // imm[5:3]
                    | ((inst >> 4) & 0x4); // imm[2]
  return LW{DecodeCL_RD(inst), DecodeCL_RS1(inst), uint32_t(offset)};
}

RISCVInst DecodeC_LD(uint32_t inst) {
  uint16_t offset = ((inst << 1) & 0xc0)    // imm[7:6]
                    | ((inst >> 7) & 0x38); // imm[5:3]
  return LD{DecodeCL_RD(inst), DecodeCL_RS1(inst), uint32_t(offset)};
}

RISCVInst DecodeC_SW(uint32_t inst) {
  uint16_t offset = ((inst << 1) & 0x40)   // imm[6]
                    | ((inst >> 7) & 0x38) // imm[5:3]
                    | ((inst >> 4) & 0x4); // imm[2]
  return SW{DecodeCS_RS1(inst), DecodeCS_RS2(inst), uint32_t(offset)};
}

RISCVInst DecodeC_SD(uint32_t inst) {
  uint16_t offset = ((inst << 1) & 0xc0)    // imm[7:6]
                    | ((inst >> 7) & 0x38); // imm[5:3]
  return SD{DecodeCS_RS1(inst), DecodeCS_RS2(inst), uint32_t(offset)};
}

RISCVInst DecodeC_J(uint32_t inst) {
  uint16_t offset = ((inst >> 1) & 0x800)   // offset[11]
                    | ((inst << 2) & 0x400) // offset[10]
                    | ((inst >> 1) & 0x300) // offset[9:8]
                    | ((inst << 1) & 0x80)  // offset[7]
                    | ((inst >> 1) & 0x40)  // offset[6]
                    | ((inst << 3) & 0x20)  // offset[5]
                    | ((inst >> 7) & 0x10)  // offset[4]
                    | ((inst >> 2) & 0xe);  // offset[3:1]
  if ((offset & 0x800) == 0)
    return JAL{Rd{0}, uint32_t(offset)};
  return JAL{Rd{0}, uint32_t(int32_t(int16_t(offset | 0xf000)))};
}

RISCVInst DecodeC_JR(uint32_t inst) {
  auto rs1 = DecodeCR_RS1(inst);
  if (rs1 == 0)
    return RESERVED{inst};
  return JALR{Rd{0}, rs1, 0};
}

RISCVInst DecodeC_JALR(uint32_t inst) {
  auto rs1 = DecodeCR_RS1(inst);
  if (rs1 == 0)
    return EBREAK{inst};
  return JALR{Rd{1}, rs1, 0};
}

constexpr uint16_t BOffset(uint32_t inst) {
  return ((inst >> 4) & 0x100)  // offset[8]
         | ((inst << 1) & 0xc0) // offset[7:6]
         | ((inst << 3) & 0x20) // offset[5]
         | ((inst >> 7) & 0x18) // offset[4:3]
         | ((inst >> 2) & 0x6); // offset[2:1]
}

RISCVInst DecodeC_BNEZ(uint32_t inst) {
  auto rs1 = DecodeCB_RS1(inst);
  uint16_t offset = BOffset(inst);
  if ((offset & 0x100) == 0)
    return B{rs1, Rs{0}, uint32_t(offset), 0b001};
  return B{rs1, Rs{0}, uint32_t(int32_t(int16_t(offset | 0xfe00))), 0b001};
}

RISCVInst DecodeC_BEQZ(uint32_t inst) {
  auto rs1 = DecodeCB_RS1(inst);
  uint16_t offset = BOffset(inst);
  if ((offset & 0x100) == 0)
    return B{rs1, Rs{0}, uint32_t(offset), 0b000};
  return B{rs1, Rs{0}, uint32_t(int32_t(int16_t(offset | 0xfe00))), 0b000};
}

RISCVInst DecodeC_LI(uint32_t inst) {
  auto rd = DecodeCI_RD(inst);
  uint16_t imm = ((inst >> 7) & 0x20) | ((inst >> 2) & 0x1f);
  if ((imm & 0x20) == 0)
    return ADDI{rd, Rs{0}, uint32_t(imm)};
  return ADDI{rd, Rs{0}, uint32_t(int32_t(int8_t(imm | 0xc0)))};
}

RISCVInst DecodeC_LUI_ADDI16SP(uint32_t inst) {
  auto rd = DecodeCI_RD(inst);
  if (rd == 0)
    return HINT{inst};
  if (rd == 2) {
    uint16_t nzimm = ((inst >> 3) & 0x200)   // nzimm[9]
                     | ((inst >> 2) & 0x10)  // nzimm[4]
                     | ((inst << 1) & 0x40)  // nzimm[6]
                     | ((inst << 4) & 0x180) // nzimm[8:7]
                     | ((inst << 3) & 0x20); // nzimm[5]
    if (nzimm == 0)
      return RESERVED{inst};
    if ((nzimm & 0x200) == 0)
      return ADDI{Rd{gpr_sp_riscv}, Rs{gpr_sp_riscv}, uint32_t(nzimm)};
    return ADDI{Rd{gpr_sp_riscv}, Rs{gpr_sp_riscv},
                uint32_t(int32_t(int16_t(nzimm | 0xfc00)))};
  }
  uint32_t imm =
      ((uint32_t(inst) << 5) & 0x20000) | ((uint32_t(inst) << 10) & 0x1f000);
  if ((imm & 0x20000) == 0)
    return LUI{rd, imm};
  return LUI{rd, uint32_t(int32_t(imm | 0xfffc0000))};
}

RISCVInst DecodeC_ADDI(uint32_t inst) {
  auto rd = DecodeCI_RD(inst);
  if (rd == 0)
    return NOP{inst};
  uint16_t imm = ((inst >> 7) & 0x20) | ((inst >> 2) & 0x1f);
  if ((imm & 0x20) == 0)
    return ADDI{rd, rd, uint32_t(imm)};
  return ADDI{rd, rd, uint32_t(int32_t(int8_t(imm | 0xc0)))};
}

RISCVInst DecodeC_ADDIW(uint32_t inst) {
  auto rd = DecodeCI_RD(inst);
  if (rd == 0)
    return RESERVED{inst};
  uint16_t imm = ((inst >> 7) & 0x20) | ((inst >> 2) & 0x1f);
  if ((imm & 0x20) == 0)
    return ADDIW{rd, rd, uint32_t(imm)};
  return ADDIW{rd, rd, uint32_t(int32_t(int8_t(imm | 0xc0)))};
}

RISCVInst DecodeC_ADDI4SPN(uint32_t inst) {
  auto rd = DecodeCIW_RD(inst);
  uint16_t nzuimm = ((inst >> 1) & 0x3c0)  // nzuimm[9:6]
                    | ((inst >> 7) & 0x30) // nzuimm[5:4]
                    | ((inst >> 2) & 0x8)  // nzuimm[3]
                    | ((inst >> 4) & 0x4); // nzuimm[2]

  if (rd == 0 && nzuimm == 0)
    return INVALID{inst};
  if (nzuimm == 0)
    return RESERVED{inst};
  return ADDI{rd, Rs{gpr_sp_riscv}, uint32_t(nzuimm)};
}

RISCVInst DecodeC_SLLI(uint32_t inst) {
  auto rd = DecodeCI_RD(inst);
  uint16_t shamt = ((inst >> 7) & 0x20) | ((inst >> 2) & 0x1f);
  if (rd == 0 || shamt == 0)
    return HINT{inst};
  return SLLI{rd, rd, uint8_t(shamt)};
}

RISCVInst DecodeC_SRLI(uint32_t inst) {
  auto rd = DecodeCB_RD(inst);
  uint16_t shamt = ((inst >> 7) & 0x20) | ((inst >> 2) & 0x1f);
  if (shamt == 0)
    return HINT{inst};
  return SRLI{rd, rd, uint8_t(shamt)};
}

RISCVInst DecodeC_SRAI(uint32_t inst) {
  auto rd = DecodeCB_RD(inst);
  uint16_t shamt = ((inst >> 7) & 0x20) | ((inst >> 2) & 0x1f);
  if (shamt == 0)
    return HINT{inst};
  return SRAI{rd, rd, uint8_t(shamt)};
}

RISCVInst DecodeC_ANDI(uint32_t inst) {
  auto rd = DecodeCB_RD(inst);
  uint16_t imm = ((inst >> 7) & 0x20) | ((inst >> 2) & 0x1f);
  if ((imm & 0x20) == 0)
    return ANDI{rd, rd, uint32_t(imm)};
  return ANDI{rd, rd, uint32_t(int32_t(int8_t(imm | 0xc0)))};
}

RISCVInst DecodeC_MV(uint32_t inst) {
  auto rd = DecodeCR_RD(inst);
  auto rs2 = DecodeCR_RS2(inst);
  if (rd == 0)
    return HINT{inst};
  return ADD{rd, Rs{0}, rs2};
}

RISCVInst DecodeC_ADD(uint32_t inst) {
  auto rd = DecodeCR_RD(inst);
  return ADD{rd, rd, DecodeCR_RS2(inst)};
}

RISCVInst DecodeC_AND(uint32_t inst) {
  auto rd = DecodeCA_RD(inst);
  return AND{rd, rd, DecodeCA_RS2(inst)};
}

RISCVInst DecodeC_OR(uint32_t inst) {
  auto rd = DecodeCA_RD(inst);
  return OR{rd, rd, DecodeCA_RS2(inst)};
}

RISCVInst DecodeC_XOR(uint32_t inst) {
  auto rd = DecodeCA_RD(inst);
  return XOR{rd, rd, DecodeCA_RS2(inst)};
}

RISCVInst DecodeC_SUB(uint32_t inst) {
  auto rd = DecodeCA_RD(inst);
  return SUB{rd, rd, DecodeCA_RS2(inst)};
}

RISCVInst DecodeC_SUBW(uint32_t inst) {
  auto rd = DecodeCA_RD(inst);
  return SUBW{rd, rd, DecodeCA_RS2(inst)};
}

RISCVInst DecodeC_ADDW(uint32_t inst) {
  auto rd = DecodeCA_RD(inst);
  return ADDW{rd, rd, DecodeCA_RS2(inst)};
}
RISCVInst DecodeC_FLW(uint32_t inst) {
  uint16_t offset = ((inst << 1) & 0x40)   // imm[6]
                    | ((inst >> 7) & 0x38) // imm[5:3]
                    | ((inst >> 4) & 0x4); // imm[2]
  return FLW{DecodeCL_RD(inst), DecodeCL_RS1(inst), uint32_t(offset)};
}

RISCVInst DecodeC_FSW(uint32_t inst) {
  uint16_t offset = ((inst << 1) & 0x40)   // imm[6]
                    | ((inst >> 7) & 0x38) // imm[5:3]
                    | ((inst >> 4) & 0x4); // imm[2]
  return FSW{DecodeCS_RS1(inst), DecodeCS_RS2(inst), uint32_t(offset)};
}

RISCVInst DecodeC_FLWSP(uint32_t inst) {
  auto rd = DecodeCI_RD(inst);
  uint16_t offset = ((inst << 4) & 0xc0)    // offset[7:6]
                    | ((inst >> 7) & 0x20)  // offset[5]
                    | ((inst >> 2) & 0x1c); // offset[4:2]
  return FLW{rd, Rs{gpr_sp_riscv}, uint32_t(offset)};
}

RISCVInst DecodeC_FSWSP(uint32_t inst) {
  uint16_t offset = ((inst >> 1) & 0xc0)    // offset[7:6]
                    | ((inst >> 7) & 0x3c); // offset[5:2]
  return FSW{Rs{gpr_sp_riscv}, DecodeCSS_RS2(inst), uint32_t(offset)};
}

RISCVInst DecodeC_FLDSP(uint32_t inst) {
  auto rd = DecodeCI_RD(inst);
  uint16_t offset = ((inst << 4) & 0x1c0)   // offset[8:6]
                    | ((inst >> 7) & 0x20)  // offset[5]
                    | ((inst >> 2) & 0x18); // offset[4:3]
  return FLD{rd, Rs{gpr_sp_riscv}, uint32_t(offset)};
}

RISCVInst DecodeC_FSDSP(uint32_t inst) {
  uint16_t offset = ((inst >> 1) & 0x1c0)   // offset[8:6]
                    | ((inst >> 7) & 0x38); // offset[5:3]
  return FSD{Rs{gpr_sp_riscv}, DecodeCSS_RS2(inst), uint32_t(offset)};
}

RISCVInst DecodeC_FLD(uint32_t inst) {
  uint16_t offset = ((inst << 1) & 0xc0)    // imm[7:6]
                    | ((inst >> 7) & 0x38); // imm[5:3]
  return FLD{DecodeCL_RD(inst), DecodeCL_RS1(inst), uint32_t(offset)};
}

RISCVInst DecodeC_FSD(uint32_t inst) {
  uint16_t offset = ((inst << 1) & 0xc0)    // imm[7:6]
                    | ((inst >> 7) & 0x38); // imm[5:3]
  return FSD{DecodeCS_RS1(inst), DecodeCS_RS2(inst), uint32_t(offset)};
}

} // namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_INSTRUCTION_RISCV_RISCVCINSTRUCTION_H
