//===-- RegisterContextWindows_x86.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__i386__) || defined(_M_IX86)

#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/windows.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private-types.h"

#include "ProcessWindowsLog.h"
#include "RegisterContextWindows_x86.h"
#include "Plugins/Process/Utility/RegisterContext_x86.h"
#include "TargetThreadWindows.h"
#include "Plugins/Process/Utility/lldb-x86-register-enums.h"

#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

#define DEFINE_GPR(reg, alt) #reg, alt, 4, 0, eEncodingUint, eFormatHexUppercase
#define DEFINE_GPR_BIN(reg, alt) #reg, alt, 4, 0, eEncodingUint, eFormatBinary

namespace {

// This enum defines the layout of the global RegisterInfo array.  This is
// necessary because lldb register sets are defined in terms of indices into
// the register array. As such, the order of RegisterInfos defined in global
// registers array must match the order defined here. When defining the
// register set layouts, these values can appear in an arbitrary order, and
// that determines the order that register values are displayed in a dump.
enum RegisterIndex {
  eRegisterIndexEax,
  eRegisterIndexEbx,
  eRegisterIndexEcx,
  eRegisterIndexEdx,
  eRegisterIndexEdi,
  eRegisterIndexEsi,
  eRegisterIndexEbp,
  eRegisterIndexEsp,
  eRegisterIndexEip,
  eRegisterIndexEflags
};

// Array of all register information supported by Windows x86
RegisterInfo g_register_infos[] = {
    //  Macro auto defines most stuff   eh_frame                DWARF
    //  GENERIC                    GDB                   LLDB
    //  VALUE REGS    INVALIDATE REGS
    //  ==============================  =======================
    //  ===================  =========================  ===================
    //  =================  ==========    ===============
    {DEFINE_GPR(eax, nullptr),
     {ehframe_eax_i386, dwarf_eax_i386, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, lldb_eax_i386},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(ebx, nullptr),
     {ehframe_ebx_i386, dwarf_ebx_i386, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, lldb_ebx_i386},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(ecx, nullptr),
     {ehframe_ecx_i386, dwarf_ecx_i386, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, lldb_ecx_i386},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(edx, nullptr),
     {ehframe_edx_i386, dwarf_edx_i386, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, lldb_edx_i386},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(edi, nullptr),
     {ehframe_edi_i386, dwarf_edi_i386, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, lldb_edi_i386},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(esi, nullptr),
     {ehframe_esi_i386, dwarf_esi_i386, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, lldb_esi_i386},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(ebp, "fp"),
     {ehframe_ebp_i386, dwarf_ebp_i386, LLDB_REGNUM_GENERIC_FP,
      LLDB_INVALID_REGNUM, lldb_ebp_i386},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(esp, "sp"),
     {ehframe_esp_i386, dwarf_esp_i386, LLDB_REGNUM_GENERIC_SP,
      LLDB_INVALID_REGNUM, lldb_esp_i386},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(eip, "pc"),
     {ehframe_eip_i386, dwarf_eip_i386, LLDB_REGNUM_GENERIC_PC,
      LLDB_INVALID_REGNUM, lldb_eip_i386},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR_BIN(eflags, "flags"),
     {ehframe_eflags_i386, dwarf_eflags_i386, LLDB_REGNUM_GENERIC_FLAGS,
      LLDB_INVALID_REGNUM, lldb_eflags_i386},
     nullptr,
     nullptr,
     nullptr,
    },
};
static size_t k_num_register_infos = std::size(g_register_infos);

// Array of lldb register numbers used to define the set of all General Purpose
// Registers
uint32_t g_gpr_reg_indices[] = {eRegisterIndexEax, eRegisterIndexEbx,
                                eRegisterIndexEcx, eRegisterIndexEdx,
                                eRegisterIndexEdi, eRegisterIndexEsi,
                                eRegisterIndexEbp, eRegisterIndexEsp,
                                eRegisterIndexEip, eRegisterIndexEflags};

RegisterSet g_register_sets[] = {
    {"General Purpose Registers", "gpr", std::size(g_gpr_reg_indices),
     g_gpr_reg_indices},
};
}

// Constructors and Destructors
RegisterContextWindows_x86::RegisterContextWindows_x86(
    Thread &thread, uint32_t concrete_frame_idx)
    : RegisterContextWindows(thread, concrete_frame_idx) {}

RegisterContextWindows_x86::~RegisterContextWindows_x86() {}

size_t RegisterContextWindows_x86::GetRegisterCount() {
  return std::size(g_register_infos);
}

const RegisterInfo *
RegisterContextWindows_x86::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < k_num_register_infos)
    return &g_register_infos[reg];
  return NULL;
}

size_t RegisterContextWindows_x86::GetRegisterSetCount() {
  return std::size(g_register_sets);
}

const RegisterSet *RegisterContextWindows_x86::GetRegisterSet(size_t reg_set) {
  return &g_register_sets[reg_set];
}

bool RegisterContextWindows_x86::ReadRegister(const RegisterInfo *reg_info,
                                              RegisterValue &reg_value) {
  if (!CacheAllRegisterValues())
    return false;

  if (reg_info == nullptr)
    return false;

  uint32_t reg = reg_info->kinds[eRegisterKindLLDB];
  switch (reg) {
  case lldb_eax_i386:
    return ReadRegisterHelper(CONTEXT_INTEGER, "EAX", m_context.Eax, reg_value);
  case lldb_ebx_i386:
    return ReadRegisterHelper(CONTEXT_INTEGER, "EBX", m_context.Ebx, reg_value);
  case lldb_ecx_i386:
    return ReadRegisterHelper(CONTEXT_INTEGER, "ECX", m_context.Ecx, reg_value);
  case lldb_edx_i386:
    return ReadRegisterHelper(CONTEXT_INTEGER, "EDX", m_context.Edx, reg_value);
  case lldb_edi_i386:
    return ReadRegisterHelper(CONTEXT_INTEGER, "EDI", m_context.Edi, reg_value);
  case lldb_esi_i386:
    return ReadRegisterHelper(CONTEXT_INTEGER, "ESI", m_context.Esi, reg_value);
  case lldb_ebp_i386:
    return ReadRegisterHelper(CONTEXT_CONTROL, "EBP", m_context.Ebp, reg_value);
  case lldb_esp_i386:
    return ReadRegisterHelper(CONTEXT_CONTROL, "ESP", m_context.Esp, reg_value);
  case lldb_eip_i386:
    return ReadRegisterHelper(CONTEXT_CONTROL, "EIP", m_context.Eip, reg_value);
  case lldb_eflags_i386:
    return ReadRegisterHelper(CONTEXT_CONTROL, "EFLAGS", m_context.EFlags,
                              reg_value);
  default:
    Log *log = GetLog(WindowsLog::Registers);
    LLDB_LOG(log, "Requested unknown register {0}", reg);
    break;
  }
  return false;
}

bool RegisterContextWindows_x86::WriteRegister(const RegisterInfo *reg_info,
                                               const RegisterValue &reg_value) {
  // Since we cannot only write a single register value to the inferior, we
  // need to make sure our cached copy of the register values are fresh.
  // Otherwise when writing EAX, for example, we may also overwrite some other
  // register with a stale value.
  if (!CacheAllRegisterValues())
    return false;

  Log *log = GetLog(WindowsLog::Registers);
  uint32_t reg = reg_info->kinds[eRegisterKindLLDB];
  switch (reg) {
  case lldb_eax_i386:
    LLDB_LOG(log, "Write value {0:x} to EAX", reg_value.GetAsUInt32());
    m_context.Eax = reg_value.GetAsUInt32();
    break;
  case lldb_ebx_i386:
    LLDB_LOG(log, "Write value {0:x} to EBX", reg_value.GetAsUInt32());
    m_context.Ebx = reg_value.GetAsUInt32();
    break;
  case lldb_ecx_i386:
    LLDB_LOG(log, "Write value {0:x} to ECX", reg_value.GetAsUInt32());
    m_context.Ecx = reg_value.GetAsUInt32();
    break;
  case lldb_edx_i386:
    LLDB_LOG(log, "Write value {0:x} to EDX", reg_value.GetAsUInt32());
    m_context.Edx = reg_value.GetAsUInt32();
    break;
  case lldb_edi_i386:
    LLDB_LOG(log, "Write value {0:x} to EDI", reg_value.GetAsUInt32());
    m_context.Edi = reg_value.GetAsUInt32();
    break;
  case lldb_esi_i386:
    LLDB_LOG(log, "Write value {0:x} to ESI", reg_value.GetAsUInt32());
    m_context.Esi = reg_value.GetAsUInt32();
    break;
  case lldb_ebp_i386:
    LLDB_LOG(log, "Write value {0:x} to EBP", reg_value.GetAsUInt32());
    m_context.Ebp = reg_value.GetAsUInt32();
    break;
  case lldb_esp_i386:
    LLDB_LOG(log, "Write value {0:x} to ESP", reg_value.GetAsUInt32());
    m_context.Esp = reg_value.GetAsUInt32();
    break;
  case lldb_eip_i386:
    LLDB_LOG(log, "Write value {0:x} to EIP", reg_value.GetAsUInt32());
    m_context.Eip = reg_value.GetAsUInt32();
    break;
  case lldb_eflags_i386:
    LLDB_LOG(log, "Write value {0:x} to EFLAGS", reg_value.GetAsUInt32());
    m_context.EFlags = reg_value.GetAsUInt32();
    break;
  default:
    LLDB_LOG(log, "Write value {0:x} to unknown register {1}",
             reg_value.GetAsUInt32(), reg);
  }

  // Physically update the registers in the target process.
  return ApplyAllRegisterValues();
}

bool RegisterContextWindows_x86::ReadRegisterHelper(
    DWORD flags_required, const char *reg_name, DWORD value,
    RegisterValue &reg_value) const {
  Log *log = GetLog(WindowsLog::Registers);
  if ((m_context.ContextFlags & flags_required) != flags_required) {
    LLDB_LOG(log, "Thread context doesn't have {0}", reg_name);
    return false;
  }
  LLDB_LOG(log, "Read value {0:x} from {1}", value, reg_name);
  reg_value.SetUInt32(value);
  return true;
}

#endif // defined(__i386__) || defined(_M_IX86)
