//===-- RegisterContextWindows_arm.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__arm__) || defined(_M_ARM)

#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/windows.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private-types.h"

#include "RegisterContextWindows_arm.h"
#include "TargetThreadWindows.h"

#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

#define GPR_OFFSET(idx) 0
#define FPU_OFFSET(idx) 0
#define FPSCR_OFFSET 0
#define EXC_OFFSET(reg) 0
#define DBG_OFFSET_NAME(reg) 0

#define DEFINE_DBG(reg, i)                                                     \
  #reg, NULL,                                                                  \
      0, DBG_OFFSET_NAME(reg[i]), eEncodingUint, eFormatHex,                   \
                              {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,       \
                               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,       \
                               LLDB_INVALID_REGNUM },                          \
                               NULL, NULL, NULL

// Include RegisterInfos_arm to declare our g_register_infos_arm structure.
#define DECLARE_REGISTER_INFOS_ARM_STRUCT
#include "Plugins/Process/Utility/RegisterInfos_arm.h"
#undef DECLARE_REGISTER_INFOS_ARM_STRUCT

static size_t k_num_register_infos = std::size(g_register_infos_arm);

// Array of lldb register numbers used to define the set of all General Purpose
// Registers
uint32_t g_gpr_reg_indices[] = {
    gpr_r0, gpr_r1,  gpr_r2,  gpr_r3,  gpr_r4, gpr_r5, gpr_r6, gpr_r7,   gpr_r8,
    gpr_r9, gpr_r10, gpr_r11, gpr_r12, gpr_sp, gpr_lr, gpr_pc, gpr_cpsr,
};

uint32_t g_fpu_reg_indices[] = {
    fpu_s0,    fpu_s1,  fpu_s2,  fpu_s3,  fpu_s4,  fpu_s5,  fpu_s6,  fpu_s7,
    fpu_s8,    fpu_s9,  fpu_s10, fpu_s11, fpu_s12, fpu_s13, fpu_s14, fpu_s15,
    fpu_s16,   fpu_s17, fpu_s18, fpu_s19, fpu_s20, fpu_s21, fpu_s22, fpu_s23,
    fpu_s24,   fpu_s25, fpu_s26, fpu_s27, fpu_s28, fpu_s29, fpu_s30, fpu_s31,

    fpu_d0,    fpu_d1,  fpu_d2,  fpu_d3,  fpu_d4,  fpu_d5,  fpu_d6,  fpu_d7,
    fpu_d8,    fpu_d9,  fpu_d10, fpu_d11, fpu_d12, fpu_d13, fpu_d14, fpu_d15,
    fpu_d16,   fpu_d17, fpu_d18, fpu_d19, fpu_d20, fpu_d21, fpu_d22, fpu_d23,
    fpu_d24,   fpu_d25, fpu_d26, fpu_d27, fpu_d28, fpu_d29, fpu_d30, fpu_d31,

    fpu_q0,    fpu_q1,  fpu_q2,  fpu_q3,  fpu_q4,  fpu_q5,  fpu_q6,  fpu_q7,
    fpu_q8,    fpu_q9,  fpu_q10, fpu_q11, fpu_q12, fpu_q13, fpu_q14, fpu_q15,

    fpu_fpscr,
};

RegisterSet g_register_sets[] = {
    {"General Purpose Registers", "gpr", std::size(g_gpr_reg_indices),
     g_gpr_reg_indices},
    {"Floating Point Registers", "fpu", std::size(g_fpu_reg_indices),
     g_fpu_reg_indices},
};

// Constructors and Destructors
RegisterContextWindows_arm::RegisterContextWindows_arm(
    Thread &thread, uint32_t concrete_frame_idx)
    : RegisterContextWindows(thread, concrete_frame_idx) {}

RegisterContextWindows_arm::~RegisterContextWindows_arm() {}

size_t RegisterContextWindows_arm::GetRegisterCount() {
  return std::size(g_register_infos_arm);
}

const RegisterInfo *
RegisterContextWindows_arm::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < k_num_register_infos)
    return &g_register_infos_arm[reg];
  return NULL;
}

size_t RegisterContextWindows_arm::GetRegisterSetCount() {
  return std::size(g_register_sets);
}

const RegisterSet *RegisterContextWindows_arm::GetRegisterSet(size_t reg_set) {
  return &g_register_sets[reg_set];
}

bool RegisterContextWindows_arm::ReadRegister(const RegisterInfo *reg_info,
                                              RegisterValue &reg_value) {
  if (!CacheAllRegisterValues())
    return false;

  if (reg_info == nullptr)
    return false;

  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];

  switch (reg) {
  case gpr_r0:
    reg_value.SetUInt32(m_context.R0);
    break;
  case gpr_r1:
    reg_value.SetUInt32(m_context.R1);
    break;
  case gpr_r2:
    reg_value.SetUInt32(m_context.R2);
    break;
  case gpr_r3:
    reg_value.SetUInt32(m_context.R3);
    break;
  case gpr_r4:
    reg_value.SetUInt32(m_context.R4);
    break;
  case gpr_r5:
    reg_value.SetUInt32(m_context.R5);
    break;
  case gpr_r6:
    reg_value.SetUInt32(m_context.R6);
    break;
  case gpr_r7:
    reg_value.SetUInt32(m_context.R7);
    break;
  case gpr_r8:
    reg_value.SetUInt32(m_context.R8);
    break;
  case gpr_r9:
    reg_value.SetUInt32(m_context.R9);
    break;
  case gpr_r10:
    reg_value.SetUInt32(m_context.R10);
    break;
  case gpr_r11:
    reg_value.SetUInt32(m_context.R11);
    break;
  case gpr_r12:
    reg_value.SetUInt32(m_context.R12);
    break;
  case gpr_sp:
    reg_value.SetUInt32(m_context.Sp);
    break;
  case gpr_lr:
    reg_value.SetUInt32(m_context.Lr);
    break;
  case gpr_pc:
    reg_value.SetUInt32(m_context.Pc);
    break;
  case gpr_cpsr:
    reg_value.SetUInt32(m_context.Cpsr);
    break;

  case fpu_s0:
  case fpu_s1:
  case fpu_s2:
  case fpu_s3:
  case fpu_s4:
  case fpu_s5:
  case fpu_s6:
  case fpu_s7:
  case fpu_s8:
  case fpu_s9:
  case fpu_s10:
  case fpu_s11:
  case fpu_s12:
  case fpu_s13:
  case fpu_s14:
  case fpu_s15:
  case fpu_s16:
  case fpu_s17:
  case fpu_s18:
  case fpu_s19:
  case fpu_s20:
  case fpu_s21:
  case fpu_s22:
  case fpu_s23:
  case fpu_s24:
  case fpu_s25:
  case fpu_s26:
  case fpu_s27:
  case fpu_s28:
  case fpu_s29:
  case fpu_s30:
  case fpu_s31:
    reg_value.SetUInt32(m_context.S[reg - fpu_s0], RegisterValue::eTypeFloat);
    break;

  case fpu_d0:
  case fpu_d1:
  case fpu_d2:
  case fpu_d3:
  case fpu_d4:
  case fpu_d5:
  case fpu_d6:
  case fpu_d7:
  case fpu_d8:
  case fpu_d9:
  case fpu_d10:
  case fpu_d11:
  case fpu_d12:
  case fpu_d13:
  case fpu_d14:
  case fpu_d15:
  case fpu_d16:
  case fpu_d17:
  case fpu_d18:
  case fpu_d19:
  case fpu_d20:
  case fpu_d21:
  case fpu_d22:
  case fpu_d23:
  case fpu_d24:
  case fpu_d25:
  case fpu_d26:
  case fpu_d27:
  case fpu_d28:
  case fpu_d29:
  case fpu_d30:
  case fpu_d31:
    reg_value.SetUInt64(m_context.D[reg - fpu_d0], RegisterValue::eTypeDouble);
    break;

  case fpu_q0:
  case fpu_q1:
  case fpu_q2:
  case fpu_q3:
  case fpu_q4:
  case fpu_q5:
  case fpu_q6:
  case fpu_q7:
  case fpu_q8:
  case fpu_q9:
  case fpu_q10:
  case fpu_q11:
  case fpu_q12:
  case fpu_q13:
  case fpu_q14:
  case fpu_q15:
    reg_value.SetBytes(&m_context.Q[reg - fpu_q0], reg_info->byte_size,
                       endian::InlHostByteOrder());
    break;

  case fpu_fpscr:
    reg_value.SetUInt32(m_context.Fpscr);
    break;

  default:
    reg_value.SetValueToInvalid();
    return false;
  }
  return true;
}

bool RegisterContextWindows_arm::WriteRegister(const RegisterInfo *reg_info,
                                               const RegisterValue &reg_value) {
  // Since we cannot only write a single register value to the inferior, we
  // need to make sure our cached copy of the register values are fresh.
  // Otherwise when writing EAX, for example, we may also overwrite some other
  // register with a stale value.
  if (!CacheAllRegisterValues())
    return false;

  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];

  switch (reg) {
  case gpr_r0:
    m_context.R0 = reg_value.GetAsUInt32();
    break;
  case gpr_r1:
    m_context.R1 = reg_value.GetAsUInt32();
    break;
  case gpr_r2:
    m_context.R2 = reg_value.GetAsUInt32();
    break;
  case gpr_r3:
    m_context.R3 = reg_value.GetAsUInt32();
    break;
  case gpr_r4:
    m_context.R4 = reg_value.GetAsUInt32();
    break;
  case gpr_r5:
    m_context.R5 = reg_value.GetAsUInt32();
    break;
  case gpr_r6:
    m_context.R6 = reg_value.GetAsUInt32();
    break;
  case gpr_r7:
    m_context.R7 = reg_value.GetAsUInt32();
    break;
  case gpr_r8:
    m_context.R8 = reg_value.GetAsUInt32();
    break;
  case gpr_r9:
    m_context.R9 = reg_value.GetAsUInt32();
    break;
  case gpr_r10:
    m_context.R10 = reg_value.GetAsUInt32();
    break;
  case gpr_r11:
    m_context.R11 = reg_value.GetAsUInt32();
    break;
  case gpr_r12:
    m_context.R12 = reg_value.GetAsUInt32();
    break;
  case gpr_sp:
    m_context.Sp = reg_value.GetAsUInt32();
    break;
  case gpr_lr:
    m_context.Lr = reg_value.GetAsUInt32();
    break;
  case gpr_pc:
    m_context.Pc = reg_value.GetAsUInt32();
    break;
  case gpr_cpsr:
    m_context.Cpsr = reg_value.GetAsUInt32();
    break;

  case fpu_s0:
  case fpu_s1:
  case fpu_s2:
  case fpu_s3:
  case fpu_s4:
  case fpu_s5:
  case fpu_s6:
  case fpu_s7:
  case fpu_s8:
  case fpu_s9:
  case fpu_s10:
  case fpu_s11:
  case fpu_s12:
  case fpu_s13:
  case fpu_s14:
  case fpu_s15:
  case fpu_s16:
  case fpu_s17:
  case fpu_s18:
  case fpu_s19:
  case fpu_s20:
  case fpu_s21:
  case fpu_s22:
  case fpu_s23:
  case fpu_s24:
  case fpu_s25:
  case fpu_s26:
  case fpu_s27:
  case fpu_s28:
  case fpu_s29:
  case fpu_s30:
  case fpu_s31:
    m_context.S[reg - fpu_s0] = reg_value.GetAsUInt32();
    break;

  case fpu_d0:
  case fpu_d1:
  case fpu_d2:
  case fpu_d3:
  case fpu_d4:
  case fpu_d5:
  case fpu_d6:
  case fpu_d7:
  case fpu_d8:
  case fpu_d9:
  case fpu_d10:
  case fpu_d11:
  case fpu_d12:
  case fpu_d13:
  case fpu_d14:
  case fpu_d15:
  case fpu_d16:
  case fpu_d17:
  case fpu_d18:
  case fpu_d19:
  case fpu_d20:
  case fpu_d21:
  case fpu_d22:
  case fpu_d23:
  case fpu_d24:
  case fpu_d25:
  case fpu_d26:
  case fpu_d27:
  case fpu_d28:
  case fpu_d29:
  case fpu_d30:
  case fpu_d31:
    m_context.D[reg - fpu_d0] = reg_value.GetAsUInt64();
    break;

  case fpu_q0:
  case fpu_q1:
  case fpu_q2:
  case fpu_q3:
  case fpu_q4:
  case fpu_q5:
  case fpu_q6:
  case fpu_q7:
  case fpu_q8:
  case fpu_q9:
  case fpu_q10:
  case fpu_q11:
  case fpu_q12:
  case fpu_q13:
  case fpu_q14:
  case fpu_q15:
    memcpy(&m_context.Q[reg - fpu_q0], reg_value.GetBytes(), 16);
    break;

  case fpu_fpscr:
    m_context.Fpscr = reg_value.GetAsUInt32();
    break;

  default:
    return false;
  }

  // Physically update the registers in the target process.
  return ApplyAllRegisterValues();
}

#endif // defined(__arm__) || defined(_M_ARM)
