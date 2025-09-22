//===-- RegisterContextDarwin_i386.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"

#include <cstddef>

#include <memory>

#include "RegisterContextDarwin_i386.h"

using namespace lldb;
using namespace lldb_private;

enum {
  gpr_eax = 0,
  gpr_ebx,
  gpr_ecx,
  gpr_edx,
  gpr_edi,
  gpr_esi,
  gpr_ebp,
  gpr_esp,
  gpr_ss,
  gpr_eflags,
  gpr_eip,
  gpr_cs,
  gpr_ds,
  gpr_es,
  gpr_fs,
  gpr_gs,

  fpu_fcw,
  fpu_fsw,
  fpu_ftw,
  fpu_fop,
  fpu_ip,
  fpu_cs,
  fpu_dp,
  fpu_ds,
  fpu_mxcsr,
  fpu_mxcsrmask,
  fpu_stmm0,
  fpu_stmm1,
  fpu_stmm2,
  fpu_stmm3,
  fpu_stmm4,
  fpu_stmm5,
  fpu_stmm6,
  fpu_stmm7,
  fpu_xmm0,
  fpu_xmm1,
  fpu_xmm2,
  fpu_xmm3,
  fpu_xmm4,
  fpu_xmm5,
  fpu_xmm6,
  fpu_xmm7,

  exc_trapno,
  exc_err,
  exc_faultvaddr,

  k_num_registers,

  // Aliases
  fpu_fctrl = fpu_fcw,
  fpu_fstat = fpu_fsw,
  fpu_ftag = fpu_ftw,
  fpu_fiseg = fpu_cs,
  fpu_fioff = fpu_ip,
  fpu_foseg = fpu_ds,
  fpu_fooff = fpu_dp
};

enum {
  ehframe_eax = 0,
  ehframe_ecx,
  ehframe_edx,
  ehframe_ebx,
  ehframe_ebp,
  ehframe_esp,
  ehframe_esi,
  ehframe_edi,
  ehframe_eip,
  ehframe_eflags
};

enum {
  dwarf_eax = 0,
  dwarf_ecx,
  dwarf_edx,
  dwarf_ebx,
  dwarf_esp,
  dwarf_ebp,
  dwarf_esi,
  dwarf_edi,
  dwarf_eip,
  dwarf_eflags,
  dwarf_stmm0 = 11,
  dwarf_stmm1,
  dwarf_stmm2,
  dwarf_stmm3,
  dwarf_stmm4,
  dwarf_stmm5,
  dwarf_stmm6,
  dwarf_stmm7,
  dwarf_xmm0 = 21,
  dwarf_xmm1,
  dwarf_xmm2,
  dwarf_xmm3,
  dwarf_xmm4,
  dwarf_xmm5,
  dwarf_xmm6,
  dwarf_xmm7
};

#define GPR_OFFSET(reg)                                                        \
  (LLVM_EXTENSION offsetof(RegisterContextDarwin_i386::GPR, reg))
#define FPU_OFFSET(reg)                                                        \
  (LLVM_EXTENSION offsetof(RegisterContextDarwin_i386::FPU, reg) +             \
   sizeof(RegisterContextDarwin_i386::GPR))
#define EXC_OFFSET(reg)                                                        \
  (LLVM_EXTENSION offsetof(RegisterContextDarwin_i386::EXC, reg) +             \
   sizeof(RegisterContextDarwin_i386::GPR) +                                   \
   sizeof(RegisterContextDarwin_i386::FPU))

// These macros will auto define the register name, alt name, register size,
// register offset, encoding, format and native register. This ensures that the
// register state structures are defined correctly and have the correct sizes
// and offsets.
#define DEFINE_GPR(reg, alt)                                                   \
  #reg, alt, sizeof(((RegisterContextDarwin_i386::GPR *) NULL)->reg),          \
                    GPR_OFFSET(reg), eEncodingUint, eFormatHex
#define DEFINE_FPU_UINT(reg)                                                   \
  #reg, NULL, sizeof(((RegisterContextDarwin_i386::FPU *) NULL)->reg),         \
                     FPU_OFFSET(reg), eEncodingUint, eFormatHex
#define DEFINE_FPU_VECT(reg, i)                                                \
  #reg #i, NULL,                                                               \
      sizeof(((RegisterContextDarwin_i386::FPU *) NULL)->reg[i].bytes),        \
              FPU_OFFSET(reg[i]), eEncodingVector, eFormatVectorOfUInt8,       \
                         {LLDB_INVALID_REGNUM, dwarf_##reg##i,                 \
                          LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,            \
                          fpu_##reg##i },                                      \
                          nullptr, nullptr, nullptr,

#define DEFINE_EXC(reg)                                                        \
  #reg, NULL, sizeof(((RegisterContextDarwin_i386::EXC *) NULL)->reg),         \
                     EXC_OFFSET(reg), eEncodingUint, eFormatHex
#define REG_CONTEXT_SIZE                                                       \
  (sizeof(RegisterContextDarwin_i386::GPR) +                                   \
   sizeof(RegisterContextDarwin_i386::FPU) +                                   \
   sizeof(RegisterContextDarwin_i386::EXC))

static RegisterInfo g_register_infos[] = {
    //  Macro auto defines most stuff   eh_frame                DWARF
    //  GENERIC                    PROCESS PLUGIN       LLDB
    //  =============================== =======================
    //  ===================   =========================  ==================
    //  =================
    {DEFINE_GPR(eax, nullptr),
     {ehframe_eax, dwarf_eax, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      gpr_eax},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(ebx, nullptr),
     {ehframe_ebx, dwarf_ebx, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      gpr_ebx},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(ecx, nullptr),
     {ehframe_ecx, dwarf_ecx, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      gpr_ecx},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(edx, nullptr),
     {ehframe_edx, dwarf_edx, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      gpr_edx},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(edi, nullptr),
     {ehframe_edi, dwarf_edi, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      gpr_edi},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(esi, nullptr),
     {ehframe_esi, dwarf_esi, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      gpr_esi},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(ebp, "fp"),
     {ehframe_ebp, dwarf_ebp, LLDB_REGNUM_GENERIC_FP, LLDB_INVALID_REGNUM,
      gpr_ebp},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(esp, "sp"),
     {ehframe_esp, dwarf_esp, LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM,
      gpr_esp},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(ss, nullptr),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, gpr_ss},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(eflags, "flags"),
     {ehframe_eflags, dwarf_eflags, LLDB_REGNUM_GENERIC_FLAGS,
      LLDB_INVALID_REGNUM, gpr_eflags},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(eip, "pc"),
     {ehframe_eip, dwarf_eip, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM,
      gpr_eip},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(cs, nullptr),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, gpr_cs},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(ds, nullptr),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, gpr_ds},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(es, nullptr),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, gpr_es},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(fs, nullptr),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, gpr_fs},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_GPR(gs, nullptr),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, gpr_gs},
     nullptr,
     nullptr,
     nullptr,
    },

    {DEFINE_FPU_UINT(fcw),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, fpu_fcw},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_FPU_UINT(fsw),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, fpu_fsw},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_FPU_UINT(ftw),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, fpu_ftw},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_FPU_UINT(fop),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, fpu_fop},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_FPU_UINT(ip),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, fpu_ip},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_FPU_UINT(cs),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, fpu_cs},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_FPU_UINT(dp),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, fpu_dp},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_FPU_UINT(ds),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, fpu_ds},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_FPU_UINT(mxcsr),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, fpu_mxcsr},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_FPU_UINT(mxcsrmask),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, fpu_mxcsrmask},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_FPU_VECT(stmm, 0)},
    {DEFINE_FPU_VECT(stmm, 1)},
    {DEFINE_FPU_VECT(stmm, 2)},
    {DEFINE_FPU_VECT(stmm, 3)},
    {DEFINE_FPU_VECT(stmm, 4)},
    {DEFINE_FPU_VECT(stmm, 5)},
    {DEFINE_FPU_VECT(stmm, 6)},
    {DEFINE_FPU_VECT(stmm, 7)},
    {DEFINE_FPU_VECT(xmm, 0)},
    {DEFINE_FPU_VECT(xmm, 1)},
    {DEFINE_FPU_VECT(xmm, 2)},
    {DEFINE_FPU_VECT(xmm, 3)},
    {DEFINE_FPU_VECT(xmm, 4)},
    {DEFINE_FPU_VECT(xmm, 5)},
    {DEFINE_FPU_VECT(xmm, 6)},
    {DEFINE_FPU_VECT(xmm, 7)},

    {DEFINE_EXC(trapno),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, exc_trapno},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_EXC(err),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, exc_err},
     nullptr,
     nullptr,
     nullptr,
    },
    {DEFINE_EXC(faultvaddr),
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM, exc_faultvaddr},
     nullptr,
     nullptr,
     nullptr,
     }};

static size_t k_num_register_infos = std::size(g_register_infos);

RegisterContextDarwin_i386::RegisterContextDarwin_i386(
    Thread &thread, uint32_t concrete_frame_idx)
    : RegisterContext(thread, concrete_frame_idx), gpr(), fpu(), exc() {
  uint32_t i;
  for (i = 0; i < kNumErrors; i++) {
    gpr_errs[i] = -1;
    fpu_errs[i] = -1;
    exc_errs[i] = -1;
  }
}

RegisterContextDarwin_i386::~RegisterContextDarwin_i386() = default;

void RegisterContextDarwin_i386::InvalidateAllRegisters() {
  InvalidateAllRegisterStates();
}

size_t RegisterContextDarwin_i386::GetRegisterCount() {
  assert(k_num_register_infos == k_num_registers);
  return k_num_registers;
}

const RegisterInfo *
RegisterContextDarwin_i386::GetRegisterInfoAtIndex(size_t reg) {
  assert(k_num_register_infos == k_num_registers);
  if (reg < k_num_registers)
    return &g_register_infos[reg];
  return nullptr;
}

size_t RegisterContextDarwin_i386::GetRegisterInfosCount() {
  return k_num_register_infos;
}

const RegisterInfo *RegisterContextDarwin_i386::GetRegisterInfos() {
  return g_register_infos;
}

// General purpose registers
static uint32_t g_gpr_regnums[] = {
    gpr_eax, gpr_ebx,    gpr_ecx, gpr_edx, gpr_edi, gpr_esi, gpr_ebp, gpr_esp,
    gpr_ss,  gpr_eflags, gpr_eip, gpr_cs,  gpr_ds,  gpr_es,  gpr_fs,  gpr_gs};

// Floating point registers
static uint32_t g_fpu_regnums[] = {
    fpu_fcw,   fpu_fsw,   fpu_ftw,   fpu_fop,       fpu_ip,    fpu_cs,
    fpu_dp,    fpu_ds,    fpu_mxcsr, fpu_mxcsrmask, fpu_stmm0, fpu_stmm1,
    fpu_stmm2, fpu_stmm3, fpu_stmm4, fpu_stmm5,     fpu_stmm6, fpu_stmm7,
    fpu_xmm0,  fpu_xmm1,  fpu_xmm2,  fpu_xmm3,      fpu_xmm4,  fpu_xmm5,
    fpu_xmm6,  fpu_xmm7};

// Exception registers

static uint32_t g_exc_regnums[] = {exc_trapno, exc_err, exc_faultvaddr};

// Number of registers in each register set
const size_t k_num_gpr_registers = std::size(g_gpr_regnums);
const size_t k_num_fpu_registers = std::size(g_fpu_regnums);
const size_t k_num_exc_registers = std::size(g_exc_regnums);

// Register set definitions. The first definitions at register set index of
// zero is for all registers, followed by other registers sets. The register
// information for the all register set need not be filled in.
static const RegisterSet g_reg_sets[] = {
    {
        "General Purpose Registers", "gpr", k_num_gpr_registers, g_gpr_regnums,
    },
    {"Floating Point Registers", "fpu", k_num_fpu_registers, g_fpu_regnums},
    {"Exception State Registers", "exc", k_num_exc_registers, g_exc_regnums}};

const size_t k_num_regsets = std::size(g_reg_sets);

size_t RegisterContextDarwin_i386::GetRegisterSetCount() {
  return k_num_regsets;
}

const RegisterSet *RegisterContextDarwin_i386::GetRegisterSet(size_t reg_set) {
  if (reg_set < k_num_regsets)
    return &g_reg_sets[reg_set];
  return nullptr;
}

// Register information definitions for 32 bit i386.
int RegisterContextDarwin_i386::GetSetForNativeRegNum(int reg_num) {
  if (reg_num < fpu_fcw)
    return GPRRegSet;
  else if (reg_num < exc_trapno)
    return FPURegSet;
  else if (reg_num < k_num_registers)
    return EXCRegSet;
  return -1;
}

void RegisterContextDarwin_i386::LogGPR(Log *log, const char *title) {
  if (log) {
    if (title)
      LLDB_LOGF(log, "%s", title);
    for (uint32_t i = 0; i < k_num_gpr_registers; i++) {
      uint32_t reg = gpr_eax + i;
      LLDB_LOGF(log, "%12s = 0x%8.8x", g_register_infos[reg].name,
                (&gpr.eax)[reg]);
    }
  }
}

int RegisterContextDarwin_i386::ReadGPR(bool force) {
  int set = GPRRegSet;
  if (force || !RegisterSetIsCached(set)) {
    SetError(set, Read, DoReadGPR(GetThreadID(), set, gpr));
  }
  return GetError(set, Read);
}

int RegisterContextDarwin_i386::ReadFPU(bool force) {
  int set = FPURegSet;
  if (force || !RegisterSetIsCached(set)) {
    SetError(set, Read, DoReadFPU(GetThreadID(), set, fpu));
  }
  return GetError(set, Read);
}

int RegisterContextDarwin_i386::ReadEXC(bool force) {
  int set = EXCRegSet;
  if (force || !RegisterSetIsCached(set)) {
    SetError(set, Read, DoReadEXC(GetThreadID(), set, exc));
  }
  return GetError(set, Read);
}

int RegisterContextDarwin_i386::WriteGPR() {
  int set = GPRRegSet;
  if (!RegisterSetIsCached(set)) {
    SetError(set, Write, -1);
    return -1;
  }
  SetError(set, Write, DoWriteGPR(GetThreadID(), set, gpr));
  SetError(set, Read, -1);
  return GetError(set, Write);
}

int RegisterContextDarwin_i386::WriteFPU() {
  int set = FPURegSet;
  if (!RegisterSetIsCached(set)) {
    SetError(set, Write, -1);
    return -1;
  }
  SetError(set, Write, DoWriteFPU(GetThreadID(), set, fpu));
  SetError(set, Read, -1);
  return GetError(set, Write);
}

int RegisterContextDarwin_i386::WriteEXC() {
  int set = EXCRegSet;
  if (!RegisterSetIsCached(set)) {
    SetError(set, Write, -1);
    return -1;
  }
  SetError(set, Write, DoWriteEXC(GetThreadID(), set, exc));
  SetError(set, Read, -1);
  return GetError(set, Write);
}

int RegisterContextDarwin_i386::ReadRegisterSet(uint32_t set, bool force) {
  switch (set) {
  case GPRRegSet:
    return ReadGPR(force);
  case FPURegSet:
    return ReadFPU(force);
  case EXCRegSet:
    return ReadEXC(force);
  default:
    break;
  }
  return -1;
}

int RegisterContextDarwin_i386::WriteRegisterSet(uint32_t set) {
  // Make sure we have a valid context to set.
  if (RegisterSetIsCached(set)) {
    switch (set) {
    case GPRRegSet:
      return WriteGPR();
    case FPURegSet:
      return WriteFPU();
    case EXCRegSet:
      return WriteEXC();
    default:
      break;
    }
  }
  return -1;
}

bool RegisterContextDarwin_i386::ReadRegister(const RegisterInfo *reg_info,
                                              RegisterValue &value) {
  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];
  int set = RegisterContextDarwin_i386::GetSetForNativeRegNum(reg);

  if (set == -1)
    return false;

  if (ReadRegisterSet(set, false) != 0)
    return false;

  switch (reg) {
  case gpr_eax:
  case gpr_ebx:
  case gpr_ecx:
  case gpr_edx:
  case gpr_edi:
  case gpr_esi:
  case gpr_ebp:
  case gpr_esp:
  case gpr_ss:
  case gpr_eflags:
  case gpr_eip:
  case gpr_cs:
  case gpr_ds:
  case gpr_es:
  case gpr_fs:
  case gpr_gs:
    value = (&gpr.eax)[reg - gpr_eax];
    break;

  case fpu_fcw:
    value = fpu.fcw;
    break;

  case fpu_fsw:
    value = fpu.fsw;
    break;

  case fpu_ftw:
    value = fpu.ftw;
    break;

  case fpu_fop:
    value = fpu.fop;
    break;

  case fpu_ip:
    value = fpu.ip;
    break;

  case fpu_cs:
    value = fpu.cs;
    break;

  case fpu_dp:
    value = fpu.dp;
    break;

  case fpu_ds:
    value = fpu.ds;
    break;

  case fpu_mxcsr:
    value = fpu.mxcsr;
    break;

  case fpu_mxcsrmask:
    value = fpu.mxcsrmask;
    break;

  case fpu_stmm0:
  case fpu_stmm1:
  case fpu_stmm2:
  case fpu_stmm3:
  case fpu_stmm4:
  case fpu_stmm5:
  case fpu_stmm6:
  case fpu_stmm7:
    // These values don't fit into scalar types,
    // RegisterContext::ReadRegisterBytes() must be used for these registers
    //::memcpy (reg_value.value.vector.uint8, fpu.stmm[reg - fpu_stmm0].bytes,
    //10);
    return false;

  case fpu_xmm0:
  case fpu_xmm1:
  case fpu_xmm2:
  case fpu_xmm3:
  case fpu_xmm4:
  case fpu_xmm5:
  case fpu_xmm6:
  case fpu_xmm7:
    // These values don't fit into scalar types,
    // RegisterContext::ReadRegisterBytes() must be used for these registers
    //::memcpy (reg_value.value.vector.uint8, fpu.xmm[reg - fpu_xmm0].bytes,
    //16);
    return false;

  case exc_trapno:
    value = exc.trapno;
    break;

  case exc_err:
    value = exc.err;
    break;

  case exc_faultvaddr:
    value = exc.faultvaddr;
    break;

  default:
    return false;
  }
  return true;
}

bool RegisterContextDarwin_i386::WriteRegister(const RegisterInfo *reg_info,
                                               const RegisterValue &value) {
  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];
  int set = GetSetForNativeRegNum(reg);

  if (set == -1)
    return false;

  if (ReadRegisterSet(set, false) != 0)
    return false;

  switch (reg) {
  case gpr_eax:
  case gpr_ebx:
  case gpr_ecx:
  case gpr_edx:
  case gpr_edi:
  case gpr_esi:
  case gpr_ebp:
  case gpr_esp:
  case gpr_ss:
  case gpr_eflags:
  case gpr_eip:
  case gpr_cs:
  case gpr_ds:
  case gpr_es:
  case gpr_fs:
  case gpr_gs:
    (&gpr.eax)[reg - gpr_eax] = value.GetAsUInt32();
    break;

  case fpu_fcw:
    fpu.fcw = value.GetAsUInt16();
    break;

  case fpu_fsw:
    fpu.fsw = value.GetAsUInt16();
    break;

  case fpu_ftw:
    fpu.ftw = value.GetAsUInt8();
    break;

  case fpu_fop:
    fpu.fop = value.GetAsUInt16();
    break;

  case fpu_ip:
    fpu.ip = value.GetAsUInt32();
    break;

  case fpu_cs:
    fpu.cs = value.GetAsUInt16();
    break;

  case fpu_dp:
    fpu.dp = value.GetAsUInt32();
    break;

  case fpu_ds:
    fpu.ds = value.GetAsUInt16();
    break;

  case fpu_mxcsr:
    fpu.mxcsr = value.GetAsUInt32();
    break;

  case fpu_mxcsrmask:
    fpu.mxcsrmask = value.GetAsUInt32();
    break;

  case fpu_stmm0:
  case fpu_stmm1:
  case fpu_stmm2:
  case fpu_stmm3:
  case fpu_stmm4:
  case fpu_stmm5:
  case fpu_stmm6:
  case fpu_stmm7:
    // These values don't fit into scalar types,
    // RegisterContext::ReadRegisterBytes() must be used for these registers
    ::memcpy(fpu.stmm[reg - fpu_stmm0].bytes, value.GetBytes(),
             value.GetByteSize());
    return false;

  case fpu_xmm0:
  case fpu_xmm1:
  case fpu_xmm2:
  case fpu_xmm3:
  case fpu_xmm4:
  case fpu_xmm5:
  case fpu_xmm6:
  case fpu_xmm7:
    // These values don't fit into scalar types,
    // RegisterContext::ReadRegisterBytes() must be used for these registers
    ::memcpy(fpu.xmm[reg - fpu_xmm0].bytes, value.GetBytes(),
             value.GetByteSize());
    return false;

  case exc_trapno:
    exc.trapno = value.GetAsUInt32();
    break;

  case exc_err:
    exc.err = value.GetAsUInt32();
    break;

  case exc_faultvaddr:
    exc.faultvaddr = value.GetAsUInt32();
    break;

  default:
    return false;
  }
  return WriteRegisterSet(set) == 0;
}

bool RegisterContextDarwin_i386::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  data_sp = std::make_shared<DataBufferHeap>(REG_CONTEXT_SIZE, 0);
  if (ReadGPR(false) == 0 && ReadFPU(false) == 0 && ReadEXC(false) == 0) {
    uint8_t *dst = data_sp->GetBytes();
    ::memcpy(dst, &gpr, sizeof(gpr));
    dst += sizeof(gpr);

    ::memcpy(dst, &fpu, sizeof(fpu));
    dst += sizeof(gpr);

    ::memcpy(dst, &exc, sizeof(exc));
    return true;
  }
  return false;
}

bool RegisterContextDarwin_i386::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  if (data_sp && data_sp->GetByteSize() == REG_CONTEXT_SIZE) {
    const uint8_t *src = data_sp->GetBytes();
    ::memcpy(&gpr, src, sizeof(gpr));
    src += sizeof(gpr);

    ::memcpy(&fpu, src, sizeof(fpu));
    src += sizeof(gpr);

    ::memcpy(&exc, src, sizeof(exc));
    uint32_t success_count = 0;
    if (WriteGPR() == 0)
      ++success_count;
    if (WriteFPU() == 0)
      ++success_count;
    if (WriteEXC() == 0)
      ++success_count;
    return success_count == 3;
  }
  return false;
}

uint32_t RegisterContextDarwin_i386::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t reg) {
  if (kind == eRegisterKindGeneric) {
    switch (reg) {
    case LLDB_REGNUM_GENERIC_PC:
      return gpr_eip;
    case LLDB_REGNUM_GENERIC_SP:
      return gpr_esp;
    case LLDB_REGNUM_GENERIC_FP:
      return gpr_ebp;
    case LLDB_REGNUM_GENERIC_FLAGS:
      return gpr_eflags;
    case LLDB_REGNUM_GENERIC_RA:
    default:
      break;
    }
  } else if (kind == eRegisterKindEHFrame || kind == eRegisterKindDWARF) {
    switch (reg) {
    case dwarf_eax:
      return gpr_eax;
    case dwarf_ecx:
      return gpr_ecx;
    case dwarf_edx:
      return gpr_edx;
    case dwarf_ebx:
      return gpr_ebx;
    case dwarf_esp:
      return gpr_esp;
    case dwarf_ebp:
      return gpr_ebp;
    case dwarf_esi:
      return gpr_esi;
    case dwarf_edi:
      return gpr_edi;
    case dwarf_eip:
      return gpr_eip;
    case dwarf_eflags:
      return gpr_eflags;
    case dwarf_stmm0:
      return fpu_stmm0;
    case dwarf_stmm1:
      return fpu_stmm1;
    case dwarf_stmm2:
      return fpu_stmm2;
    case dwarf_stmm3:
      return fpu_stmm3;
    case dwarf_stmm4:
      return fpu_stmm4;
    case dwarf_stmm5:
      return fpu_stmm5;
    case dwarf_stmm6:
      return fpu_stmm6;
    case dwarf_stmm7:
      return fpu_stmm7;
    case dwarf_xmm0:
      return fpu_xmm0;
    case dwarf_xmm1:
      return fpu_xmm1;
    case dwarf_xmm2:
      return fpu_xmm2;
    case dwarf_xmm3:
      return fpu_xmm3;
    case dwarf_xmm4:
      return fpu_xmm4;
    case dwarf_xmm5:
      return fpu_xmm5;
    case dwarf_xmm6:
      return fpu_xmm6;
    case dwarf_xmm7:
      return fpu_xmm7;
    default:
      break;
    }
  } else if (kind == eRegisterKindLLDB) {
    return reg;
  }
  return LLDB_INVALID_REGNUM;
}

bool RegisterContextDarwin_i386::HardwareSingleStep(bool enable) {
  if (ReadGPR(false) != 0)
    return false;

  const uint32_t trace_bit = 0x100u;
  if (enable) {
    // If the trace bit is already set, there is nothing to do
    if (gpr.eflags & trace_bit)
      return true;
    else
      gpr.eflags |= trace_bit;
  } else {
    // If the trace bit is already cleared, there is nothing to do
    if (gpr.eflags & trace_bit)
      gpr.eflags &= ~trace_bit;
    else
      return true;
  }

  return WriteGPR() == 0;
}
