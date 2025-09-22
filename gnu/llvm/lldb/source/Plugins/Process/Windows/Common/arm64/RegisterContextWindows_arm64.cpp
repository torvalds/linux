//===-- RegisterContextWindows_arm64.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__aarch64__) || defined(_M_ARM64)

#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/windows.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private-types.h"

#include "RegisterContextWindows_arm64.h"
#include "TargetThreadWindows.h"

#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

#define GPR_OFFSET(idx) 0
#define GPR_OFFSET_NAME(reg) 0

#define FPU_OFFSET(idx) 0
#define FPU_OFFSET_NAME(reg) 0

#define EXC_OFFSET_NAME(reg) 0
#define DBG_OFFSET_NAME(reg) 0

#define DEFINE_DBG(reg, i)                                                     \
  #reg, NULL,                                                                  \
      0, DBG_OFFSET_NAME(reg[i]), eEncodingUint, eFormatHex,                   \
                              {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,       \
                               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,       \
                               LLDB_INVALID_REGNUM },                          \
                               NULL, NULL, NULL

// Include RegisterInfos_arm64 to declare our g_register_infos_arm64 structure.
#define DECLARE_REGISTER_INFOS_ARM64_STRUCT
#include "Plugins/Process/Utility/RegisterInfos_arm64.h"
#undef DECLARE_REGISTER_INFOS_ARM64_STRUCT

static size_t k_num_register_infos = std::size(g_register_infos_arm64_le);

// Array of lldb register numbers used to define the set of all General Purpose
// Registers
uint32_t g_gpr_reg_indices[] = {
    gpr_x0,  gpr_x1,   gpr_x2,  gpr_x3,  gpr_x4,  gpr_x5,  gpr_x6,  gpr_x7,
    gpr_x8,  gpr_x9,   gpr_x10, gpr_x11, gpr_x12, gpr_x13, gpr_x14, gpr_x15,
    gpr_x16, gpr_x17,  gpr_x18, gpr_x19, gpr_x20, gpr_x21, gpr_x22, gpr_x23,
    gpr_x24, gpr_x25,  gpr_x26, gpr_x27, gpr_x28, gpr_fp,  gpr_lr,  gpr_sp,
    gpr_pc,  gpr_cpsr,

    gpr_w0,  gpr_w1,   gpr_w2,  gpr_w3,  gpr_w4,  gpr_w5,  gpr_w6,  gpr_w7,
    gpr_w8,  gpr_w9,   gpr_w10, gpr_w11, gpr_w12, gpr_w13, gpr_w14, gpr_w15,
    gpr_w16, gpr_w17,  gpr_w18, gpr_w19, gpr_w20, gpr_w21, gpr_w22, gpr_w23,
    gpr_w24, gpr_w25,  gpr_w26, gpr_w27, gpr_w28,
};

uint32_t g_fpu_reg_indices[] = {
    fpu_v0,   fpu_v1,   fpu_v2,  fpu_v3,  fpu_v4,  fpu_v5,  fpu_v6,  fpu_v7,
    fpu_v8,   fpu_v9,   fpu_v10, fpu_v11, fpu_v12, fpu_v13, fpu_v14, fpu_v15,
    fpu_v16,  fpu_v17,  fpu_v18, fpu_v19, fpu_v20, fpu_v21, fpu_v22, fpu_v23,
    fpu_v24,  fpu_v25,  fpu_v26, fpu_v27, fpu_v28, fpu_v29, fpu_v30, fpu_v31,

    fpu_s0,   fpu_s1,   fpu_s2,  fpu_s3,  fpu_s4,  fpu_s5,  fpu_s6,  fpu_s7,
    fpu_s8,   fpu_s9,   fpu_s10, fpu_s11, fpu_s12, fpu_s13, fpu_s14, fpu_s15,
    fpu_s16,  fpu_s17,  fpu_s18, fpu_s19, fpu_s20, fpu_s21, fpu_s22, fpu_s23,
    fpu_s24,  fpu_s25,  fpu_s26, fpu_s27, fpu_s28, fpu_s29, fpu_s30, fpu_s31,

    fpu_d0,   fpu_d1,   fpu_d2,  fpu_d3,  fpu_d4,  fpu_d5,  fpu_d6,  fpu_d7,
    fpu_d8,   fpu_d9,   fpu_d10, fpu_d11, fpu_d12, fpu_d13, fpu_d14, fpu_d15,
    fpu_d16,  fpu_d17,  fpu_d18, fpu_d19, fpu_d20, fpu_d21, fpu_d22, fpu_d23,
    fpu_d24,  fpu_d25,  fpu_d26, fpu_d27, fpu_d28, fpu_d29, fpu_d30, fpu_d31,

    fpu_fpsr, fpu_fpcr,
};

RegisterSet g_register_sets[] = {
    {"General Purpose Registers", "gpr", std::size(g_gpr_reg_indices),
     g_gpr_reg_indices},
    {"Floating Point Registers", "fpu", std::size(g_fpu_reg_indices),
     g_fpu_reg_indices},
};

// Constructors and Destructors
RegisterContextWindows_arm64::RegisterContextWindows_arm64(
    Thread &thread, uint32_t concrete_frame_idx)
    : RegisterContextWindows(thread, concrete_frame_idx) {}

RegisterContextWindows_arm64::~RegisterContextWindows_arm64() {}

size_t RegisterContextWindows_arm64::GetRegisterCount() {
  return std::size(g_register_infos_arm64_le);
}

const RegisterInfo *
RegisterContextWindows_arm64::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < k_num_register_infos)
    return &g_register_infos_arm64_le[reg];
  return NULL;
}

size_t RegisterContextWindows_arm64::GetRegisterSetCount() {
  return std::size(g_register_sets);
}

const RegisterSet *
RegisterContextWindows_arm64::GetRegisterSet(size_t reg_set) {
  return &g_register_sets[reg_set];
}

bool RegisterContextWindows_arm64::ReadRegister(const RegisterInfo *reg_info,
                                                RegisterValue &reg_value) {
  if (!CacheAllRegisterValues())
    return false;

  if (reg_info == nullptr)
    return false;

  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];

  switch (reg) {
  case gpr_x0:
  case gpr_x1:
  case gpr_x2:
  case gpr_x3:
  case gpr_x4:
  case gpr_x5:
  case gpr_x6:
  case gpr_x7:
  case gpr_x8:
  case gpr_x9:
  case gpr_x10:
  case gpr_x11:
  case gpr_x12:
  case gpr_x13:
  case gpr_x14:
  case gpr_x15:
  case gpr_x16:
  case gpr_x17:
  case gpr_x18:
  case gpr_x19:
  case gpr_x20:
  case gpr_x21:
  case gpr_x22:
  case gpr_x23:
  case gpr_x24:
  case gpr_x25:
  case gpr_x26:
  case gpr_x27:
  case gpr_x28:
    reg_value.SetUInt64(m_context.X[reg - gpr_x0]);
    break;

  case gpr_fp:
    reg_value.SetUInt64(m_context.Fp);
    break;
  case gpr_sp:
    reg_value.SetUInt64(m_context.Sp);
    break;
  case gpr_lr:
    reg_value.SetUInt64(m_context.Lr);
    break;
  case gpr_pc:
    reg_value.SetUInt64(m_context.Pc);
    break;
  case gpr_cpsr:
    reg_value.SetUInt32(m_context.Cpsr);
    break;

  case gpr_w0:
  case gpr_w1:
  case gpr_w2:
  case gpr_w3:
  case gpr_w4:
  case gpr_w5:
  case gpr_w6:
  case gpr_w7:
  case gpr_w8:
  case gpr_w9:
  case gpr_w10:
  case gpr_w11:
  case gpr_w12:
  case gpr_w13:
  case gpr_w14:
  case gpr_w15:
  case gpr_w16:
  case gpr_w17:
  case gpr_w18:
  case gpr_w19:
  case gpr_w20:
  case gpr_w21:
  case gpr_w22:
  case gpr_w23:
  case gpr_w24:
  case gpr_w25:
  case gpr_w26:
  case gpr_w27:
  case gpr_w28:
    reg_value.SetUInt32(
        static_cast<uint32_t>(m_context.X[reg - gpr_w0] & 0xffffffff));
    break;

  case fpu_v0:
  case fpu_v1:
  case fpu_v2:
  case fpu_v3:
  case fpu_v4:
  case fpu_v5:
  case fpu_v6:
  case fpu_v7:
  case fpu_v8:
  case fpu_v9:
  case fpu_v10:
  case fpu_v11:
  case fpu_v12:
  case fpu_v13:
  case fpu_v14:
  case fpu_v15:
  case fpu_v16:
  case fpu_v17:
  case fpu_v18:
  case fpu_v19:
  case fpu_v20:
  case fpu_v21:
  case fpu_v22:
  case fpu_v23:
  case fpu_v24:
  case fpu_v25:
  case fpu_v26:
  case fpu_v27:
  case fpu_v28:
  case fpu_v29:
  case fpu_v30:
  case fpu_v31:
    reg_value.SetBytes(m_context.V[reg - fpu_v0].B, reg_info->byte_size,
                       endian::InlHostByteOrder());
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
    reg_value.SetFloat(m_context.V[reg - fpu_s0].S[0]);
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
    reg_value.SetDouble(m_context.V[reg - fpu_d0].D[0]);
    break;

  case fpu_fpsr:
    reg_value.SetUInt32(m_context.Fpsr);
    break;

  case fpu_fpcr:
    reg_value.SetUInt32(m_context.Fpcr);
    break;

  default:
    reg_value.SetValueToInvalid();
    return false;
  }
  return true;
}

bool RegisterContextWindows_arm64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {
  // Since we cannot only write a single register value to the inferior, we
  // need to make sure our cached copy of the register values are fresh.
  // Otherwise when writing one register, we may also overwrite some other
  // register with a stale value.
  if (!CacheAllRegisterValues())
    return false;

  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];

  switch (reg) {
  case gpr_x0:
  case gpr_x1:
  case gpr_x2:
  case gpr_x3:
  case gpr_x4:
  case gpr_x5:
  case gpr_x6:
  case gpr_x7:
  case gpr_x8:
  case gpr_x9:
  case gpr_x10:
  case gpr_x11:
  case gpr_x12:
  case gpr_x13:
  case gpr_x14:
  case gpr_x15:
  case gpr_x16:
  case gpr_x17:
  case gpr_x18:
  case gpr_x19:
  case gpr_x20:
  case gpr_x21:
  case gpr_x22:
  case gpr_x23:
  case gpr_x24:
  case gpr_x25:
  case gpr_x26:
  case gpr_x27:
  case gpr_x28:
    m_context.X[reg - gpr_x0] = reg_value.GetAsUInt64();
    break;

  case gpr_fp:
    m_context.Fp = reg_value.GetAsUInt64();
    break;
  case gpr_sp:
    m_context.Sp = reg_value.GetAsUInt64();
    break;
  case gpr_lr:
    m_context.Lr = reg_value.GetAsUInt64();
    break;
  case gpr_pc:
    m_context.Pc = reg_value.GetAsUInt64();
    break;
  case gpr_cpsr:
    m_context.Cpsr = reg_value.GetAsUInt32();
    break;

  case fpu_v0:
  case fpu_v1:
  case fpu_v2:
  case fpu_v3:
  case fpu_v4:
  case fpu_v5:
  case fpu_v6:
  case fpu_v7:
  case fpu_v8:
  case fpu_v9:
  case fpu_v10:
  case fpu_v11:
  case fpu_v12:
  case fpu_v13:
  case fpu_v14:
  case fpu_v15:
  case fpu_v16:
  case fpu_v17:
  case fpu_v18:
  case fpu_v19:
  case fpu_v20:
  case fpu_v21:
  case fpu_v22:
  case fpu_v23:
  case fpu_v24:
  case fpu_v25:
  case fpu_v26:
  case fpu_v27:
  case fpu_v28:
  case fpu_v29:
  case fpu_v30:
  case fpu_v31:
    memcpy(m_context.V[reg - fpu_v0].B, reg_value.GetBytes(), 16);
    break;

  case fpu_fpsr:
    m_context.Fpsr = reg_value.GetAsUInt32();
    break;

  case fpu_fpcr:
    m_context.Fpcr = reg_value.GetAsUInt32();
    break;

  default:
    return false;
  }

  // Physically update the registers in the target process.
  return ApplyAllRegisterValues();
}

#endif // defined(__aarch64__) || defined(_M_ARM64)
