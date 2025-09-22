//===-- RegisterInfos_powerpc.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include <cstddef>

// Computes the offset of the given GPR in the user data area.
#define GPR_OFFSET(regname) (offsetof(GPR, regname))
#define FPR_OFFSET(regname) (sizeof(GPR) + offsetof(FPR, regname))
#define VMX_OFFSET(regname) (sizeof(GPR) + sizeof(FPR) + offsetof(VMX, regname))
#define GPR_SIZE(regname) (sizeof(((GPR *)NULL)->regname))

#ifdef DECLARE_REGISTER_INFOS_POWERPC_STRUCT

// Note that the size and offset will be updated by platform-specific classes.
#define DEFINE_GPR(reg, alt, lldb_kind)                                        \
  {                                                                            \
    #reg, alt, GPR_SIZE(reg), GPR_OFFSET(reg), eEncodingUint, eFormatHex,      \
                                         {dwarf_##reg##_powerpc,               \
                                          dwarf_##reg##_powerpc, lldb_kind,    \
                                          LLDB_INVALID_REGNUM,                 \
                                          gpr_##reg##_powerpc },               \
                                          NULL, NULL, NULL,                    \
  }
#define DEFINE_FPR(reg, lldb_kind)                                             \
  {                                                                            \
    #reg, NULL, 8, FPR_OFFSET(reg), eEncodingIEEE754, eFormatFloat,            \
                              {dwarf_##reg##_powerpc, dwarf_##reg##_powerpc,   \
                               lldb_kind, LLDB_INVALID_REGNUM,                 \
                               fpr_##reg##_powerpc },                          \
                               NULL, NULL, NULL,                               \
  }
#define DEFINE_VMX(reg, lldb_kind)                                             \
  {                                                                            \
    #reg, NULL, 16, VMX_OFFSET(reg), eEncodingVector, eFormatVectorOfUInt32,   \
                               {dwarf_##reg##_powerpc, dwarf_##reg##_powerpc,  \
                                lldb_kind, LLDB_INVALID_REGNUM,                \
                                vmx_##reg##_powerpc },                         \
                                NULL, NULL, NULL,                              \
  }

// General purpose registers.            EH_Frame,                  DWARF,
// Generic,                Process Plugin
#define POWERPC_REGS                                                           \
  DEFINE_GPR(r0, NULL, LLDB_INVALID_REGNUM)                                    \
  , DEFINE_GPR(r1, NULL, LLDB_REGNUM_GENERIC_SP),                              \
      DEFINE_GPR(r2, NULL, LLDB_INVALID_REGNUM),                               \
      DEFINE_GPR(r3, NULL, LLDB_REGNUM_GENERIC_ARG1),                          \
      DEFINE_GPR(r4, NULL, LLDB_REGNUM_GENERIC_ARG2),                          \
      DEFINE_GPR(r5, NULL, LLDB_REGNUM_GENERIC_ARG3),                          \
      DEFINE_GPR(r6, NULL, LLDB_REGNUM_GENERIC_ARG4),                          \
      DEFINE_GPR(r7, NULL, LLDB_REGNUM_GENERIC_ARG5),                          \
      DEFINE_GPR(r8, NULL, LLDB_REGNUM_GENERIC_ARG6),                          \
      DEFINE_GPR(r9, NULL, LLDB_REGNUM_GENERIC_ARG7),                          \
      DEFINE_GPR(r10, NULL, LLDB_REGNUM_GENERIC_ARG8),                         \
      DEFINE_GPR(r11, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r12, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r13, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r14, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r15, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r16, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r17, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r18, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r19, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r20, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r21, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r22, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r23, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r24, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r25, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r26, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r27, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r28, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r29, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r30, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(r31, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(lr, NULL, LLDB_REGNUM_GENERIC_RA),                            \
      DEFINE_GPR(cr, NULL, LLDB_REGNUM_GENERIC_FLAGS),                         \
      DEFINE_GPR(xer, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(ctr, NULL, LLDB_INVALID_REGNUM),                              \
      DEFINE_GPR(pc, NULL, LLDB_REGNUM_GENERIC_PC),                            \
      DEFINE_FPR(f0, LLDB_INVALID_REGNUM),                                     \
      DEFINE_FPR(f1, LLDB_INVALID_REGNUM),                                     \
      DEFINE_FPR(f2, LLDB_INVALID_REGNUM),                                     \
      DEFINE_FPR(f3, LLDB_INVALID_REGNUM),                                     \
      DEFINE_FPR(f4, LLDB_INVALID_REGNUM),                                     \
      DEFINE_FPR(f5, LLDB_INVALID_REGNUM),                                     \
      DEFINE_FPR(f6, LLDB_INVALID_REGNUM),                                     \
      DEFINE_FPR(f7, LLDB_INVALID_REGNUM),                                     \
      DEFINE_FPR(f8, LLDB_INVALID_REGNUM),                                     \
      DEFINE_FPR(f9, LLDB_INVALID_REGNUM),                                     \
      DEFINE_FPR(f10, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f11, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f12, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f13, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f14, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f15, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f16, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f17, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f18, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f19, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f20, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f21, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f22, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f23, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f24, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f25, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f26, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f27, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f28, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f29, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f30, LLDB_INVALID_REGNUM),                                    \
      DEFINE_FPR(f31, LLDB_INVALID_REGNUM),                                    \
      {"fpscr",                                                                \
       NULL,                                                                   \
       8,                                                                      \
       FPR_OFFSET(fpscr),                                                      \
       eEncodingUint,                                                          \
       eFormatHex,                                                             \
       {dwarf_fpscr_powerpc, dwarf_fpscr_powerpc, LLDB_INVALID_REGNUM,         \
        LLDB_INVALID_REGNUM, fpr_fpscr_powerpc},                               \
       NULL,                                                                   \
       NULL,                                                                   \
       NULL,                                                                   \
        },                                                                     \
      DEFINE_VMX(v0, LLDB_INVALID_REGNUM),                                     \
      DEFINE_VMX(v1, LLDB_INVALID_REGNUM),                                     \
      DEFINE_VMX(v2, LLDB_INVALID_REGNUM),                                     \
      DEFINE_VMX(v3, LLDB_INVALID_REGNUM),                                     \
      DEFINE_VMX(v4, LLDB_INVALID_REGNUM),                                     \
      DEFINE_VMX(v5, LLDB_INVALID_REGNUM),                                     \
      DEFINE_VMX(v6, LLDB_INVALID_REGNUM),                                     \
      DEFINE_VMX(v7, LLDB_INVALID_REGNUM),                                     \
      DEFINE_VMX(v8, LLDB_INVALID_REGNUM),                                     \
      DEFINE_VMX(v9, LLDB_INVALID_REGNUM),                                     \
      DEFINE_VMX(v10, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v11, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v12, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v13, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v14, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v15, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v16, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v17, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v18, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v19, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v20, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v21, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v22, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v23, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v24, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v25, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v26, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v27, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v28, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v29, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v30, LLDB_INVALID_REGNUM),                                    \
      DEFINE_VMX(v31, LLDB_INVALID_REGNUM),                                    \
      {"vrsave",                                                               \
       NULL,                                                                   \
       4,                                                                      \
       VMX_OFFSET(vrsave),                                                     \
       eEncodingUint,                                                          \
       eFormatHex,                                                             \
       {dwarf_vrsave_powerpc, dwarf_vrsave_powerpc, LLDB_INVALID_REGNUM,       \
        LLDB_INVALID_REGNUM, vmx_vrsave_powerpc},                              \
       NULL,                                                                   \
       NULL,                                                                   \
       NULL,                                                                   \
        },                                                                     \
      {"vscr",                                                                 \
       NULL,                                                                   \
       4,                                                                      \
       VMX_OFFSET(vscr),                                                       \
       eEncodingUint,                                                          \
       eFormatHex,                                                             \
       {dwarf_vscr_powerpc, dwarf_vscr_powerpc, LLDB_INVALID_REGNUM,           \
        LLDB_INVALID_REGNUM, vmx_vscr_powerpc},                                \
       NULL,                                                                   \
       NULL,                                                                   \
       NULL,                                                                   \
       },

static RegisterInfo g_register_infos_powerpc64[] = {
#define GPR GPR64
    POWERPC_REGS
#undef GPR
};

static RegisterInfo g_register_infos_powerpc32[] = {
#define GPR GPR32
    POWERPC_REGS
#undef GPR
};

static RegisterInfo g_register_infos_powerpc64_32[] = {
#define GPR GPR64
#undef GPR_SIZE
#define GPR_SIZE(reg) (sizeof(uint32_t))
#undef GPR_OFFSET
#define GPR_OFFSET(regname)                                                    \
  (offsetof(GPR, regname) + (sizeof(((GPR *)NULL)->regname) - GPR_SIZE(reg)))
    POWERPC_REGS
#undef GPR
};

static_assert((sizeof(g_register_infos_powerpc32) /
               sizeof(g_register_infos_powerpc32[0])) ==
                  k_num_registers_powerpc,
              "g_register_infos_powerpc32 has wrong number of register infos");
static_assert((sizeof(g_register_infos_powerpc64) /
               sizeof(g_register_infos_powerpc64[0])) ==
                  k_num_registers_powerpc,
              "g_register_infos_powerpc64 has wrong number of register infos");
static_assert(sizeof(g_register_infos_powerpc64_32) ==
                  sizeof(g_register_infos_powerpc64),
              "g_register_infos_powerpc64_32 doesn't match size of "
              "g_register_infos_powerpc64");

#undef DEFINE_FPR
#undef DEFINE_GPR

#endif // DECLARE_REGISTER_INFOS_POWERPC_STRUCT

#undef GPR_OFFSET
