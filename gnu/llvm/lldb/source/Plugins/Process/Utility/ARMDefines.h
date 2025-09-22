//===-- ARMDefines.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_ARMDEFINES_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_ARMDEFINES_H

#include "llvm/Support/ErrorHandling.h"

#include <cassert>
#include <cstdint>

// Common definitions for the ARM/Thumb Instruction Set Architecture.

namespace lldb_private {

// ARM shifter types
enum ARM_ShifterType {
  SRType_LSL,
  SRType_LSR,
  SRType_ASR,
  SRType_ROR,
  SRType_RRX,
  SRType_Invalid
};

// ARM conditions          // Meaning (integer)         Meaning (floating-point)
// Condition flags
#define COND_EQ                                                                \
  0x0 // Equal                     Equal                         Z == 1
#define COND_NE                                                                \
  0x1 // Not equal                 Not equal, or unordered       Z == 0
#define COND_CS                                                                \
  0x2 // Carry set                 >, ==, or unordered           C == 1
#define COND_HS 0x2
#define COND_CC                                                                \
  0x3 // Carry clear               Less than                     C == 0
#define COND_LO 0x3
#define COND_MI                                                                \
  0x4 // Minus, negative           Less than                     N == 1
#define COND_PL                                                                \
  0x5 // Plus, positive or zero    >, ==, or unordered           N == 0
#define COND_VS                                                                \
  0x6 // Overflow                  Unordered                     V == 1
#define COND_VC                                                                \
  0x7 // No overflow               Not unordered                 V == 0
#define COND_HI                                                                \
  0x8 // Unsigned higher           Greater than, or unordered    C == 1 and Z ==
      // 0
#define COND_LS                                                                \
  0x9 // Unsigned lower or same    Less than or equal            C == 0 or Z ==
      // 1
#define COND_GE                                                                \
  0xA // Greater than or equal     Greater than or equal         N == V
#define COND_LT                                                                \
  0xB // Less than                 Less than, or unordered       N != V
#define COND_GT                                                                \
  0xC // Greater than              Greater than                  Z == 0 and N ==
      // V
#define COND_LE                                                                \
  0xD // Less than or equal        <, ==, or unordered           Z == 1 or N !=
      // V
#define COND_AL                                                                \
  0xE // Always (unconditional)    Always (unconditional)        Any
#define COND_UNCOND 0xF

static inline const char *ARMCondCodeToString(uint32_t CC) {
  switch (CC) {
  case COND_EQ:
    return "eq";
  case COND_NE:
    return "ne";
  case COND_HS:
    return "hs";
  case COND_LO:
    return "lo";
  case COND_MI:
    return "mi";
  case COND_PL:
    return "pl";
  case COND_VS:
    return "vs";
  case COND_VC:
    return "vc";
  case COND_HI:
    return "hi";
  case COND_LS:
    return "ls";
  case COND_GE:
    return "ge";
  case COND_LT:
    return "lt";
  case COND_GT:
    return "gt";
  case COND_LE:
    return "le";
  case COND_AL:
    return "al";
  }
  llvm_unreachable("Unknown condition code");
}

static inline bool ARMConditionPassed(const uint32_t condition,
                                      const uint32_t cpsr) {
  const uint32_t cpsr_n = (cpsr >> 31) & 1u; // Negative condition code flag
  const uint32_t cpsr_z = (cpsr >> 30) & 1u; // Zero condition code flag
  const uint32_t cpsr_c = (cpsr >> 29) & 1u; // Carry condition code flag
  const uint32_t cpsr_v = (cpsr >> 28) & 1u; // Overflow condition code flag

  switch (condition) {
  case COND_EQ:
    return (cpsr_z == 1);
  case COND_NE:
    return (cpsr_z == 0);
  case COND_CS:
    return (cpsr_c == 1);
  case COND_CC:
    return (cpsr_c == 0);
  case COND_MI:
    return (cpsr_n == 1);
  case COND_PL:
    return (cpsr_n == 0);
  case COND_VS:
    return (cpsr_v == 1);
  case COND_VC:
    return (cpsr_v == 0);
  case COND_HI:
    return ((cpsr_c == 1) && (cpsr_z == 0));
  case COND_LS:
    return ((cpsr_c == 0) || (cpsr_z == 1));
  case COND_GE:
    return (cpsr_n == cpsr_v);
  case COND_LT:
    return (cpsr_n != cpsr_v);
  case COND_GT:
    return ((cpsr_z == 0) && (cpsr_n == cpsr_v));
  case COND_LE:
    return ((cpsr_z == 1) || (cpsr_n != cpsr_v));
  case COND_AL:
  case COND_UNCOND:
  default:
    return true;
  }
  return false;
}

// Bit positions for CPSR
#define CPSR_T_POS 5
#define CPSR_F_POS 6
#define CPSR_I_POS 7
#define CPSR_A_POS 8
#define CPSR_E_POS 9
#define CPSR_J_POS 24
#define CPSR_Q_POS 27
#define CPSR_V_POS 28
#define CPSR_C_POS 29
#define CPSR_Z_POS 30
#define CPSR_N_POS 31

// CPSR mode definitions
#define CPSR_MODE_USR 0x10u
#define CPSR_MODE_FIQ 0x11u
#define CPSR_MODE_IRQ 0x12u
#define CPSR_MODE_SVC 0x13u
#define CPSR_MODE_ABT 0x17u
#define CPSR_MODE_UND 0x1bu
#define CPSR_MODE_SYS 0x1fu

// Masks for CPSR
#define MASK_CPSR_MODE_MASK (0x0000001fu)
#define MASK_CPSR_IT_MASK (0x0600fc00u)
#define MASK_CPSR_T (1u << CPSR_T_POS)
#define MASK_CPSR_F (1u << CPSR_F_POS)
#define MASK_CPSR_I (1u << CPSR_I_POS)
#define MASK_CPSR_A (1u << CPSR_A_POS)
#define MASK_CPSR_E (1u << CPSR_E_POS)
#define MASK_CPSR_GE_MASK (0x000f0000u)
#define MASK_CPSR_J (1u << CPSR_J_POS)
#define MASK_CPSR_Q (1u << CPSR_Q_POS)
#define MASK_CPSR_V (1u << CPSR_V_POS)
#define MASK_CPSR_C (1u << CPSR_C_POS)
#define MASK_CPSR_Z (1u << CPSR_Z_POS)
#define MASK_CPSR_N (1u << CPSR_N_POS)

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_ARMDEFINES_H
