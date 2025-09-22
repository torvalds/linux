//===-- RegisterInfos_ppc64.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifdef DECLARE_REGISTER_INFOS_PPC64_STRUCT

#include <cstddef>

// Computes the offset of the given GPR_PPC64 in the user data area.
#define GPR_PPC64_OFFSET(regname) (offsetof(GPR_PPC64, regname))
#define FPR_PPC64_OFFSET(regname) (offsetof(FPR_PPC64, regname)                \
                                   + sizeof(GPR_PPC64))
#define VMX_PPC64_OFFSET(regname) (offsetof(VMX_PPC64, regname)                \
                                   + sizeof(GPR_PPC64) + sizeof(FPR_PPC64))
#define GPR_PPC64_SIZE(regname) (sizeof(((GPR_PPC64 *)NULL)->regname))

#include "Utility/PPC64_DWARF_Registers.h"
#include "lldb-ppc64-register-enums.h"

// Note that the size and offset will be updated by platform-specific classes.
#define DEFINE_GPR_PPC64(reg, alt, lldb_kind)                                  \
  {                                                                            \
    #reg, alt, GPR_PPC64_SIZE(reg), GPR_PPC64_OFFSET(reg), lldb::eEncodingUint,\
                                         lldb::eFormatHex,                     \
                                         {ppc64_dwarf::dwarf_##reg##_ppc64,    \
                                          ppc64_dwarf::dwarf_##reg##_ppc64,    \
                                          lldb_kind,                           \
                                          LLDB_INVALID_REGNUM,                 \
                                          gpr_##reg##_ppc64 },                 \
                                          NULL, NULL, NULL,                    \
  }
#define DEFINE_FPR_PPC64(reg, alt, lldb_kind)                                  \
  {                                                                            \
#reg, alt, 8, FPR_PPC64_OFFSET(reg), lldb::eEncodingIEEE754,                   \
        lldb::eFormatFloat,                                                    \
        {ppc64_dwarf::dwarf_##reg##_ppc64,                                     \
         ppc64_dwarf::dwarf_##reg##_ppc64, lldb_kind, LLDB_INVALID_REGNUM,     \
         fpr_##reg##_ppc64 },                                                  \
         NULL, NULL, NULL,                                                     \
  }
#define DEFINE_VMX_PPC64(reg, lldb_kind)                                       \
  {                                                                            \
#reg, NULL, 16, VMX_PPC64_OFFSET(reg), lldb::eEncodingVector,                  \
        lldb::eFormatVectorOfUInt32,                                           \
        {ppc64_dwarf::dwarf_##reg##_ppc64,                                     \
         ppc64_dwarf::dwarf_##reg##_ppc64, lldb_kind, LLDB_INVALID_REGNUM,     \
         vmx_##reg##_ppc64 },                                                  \
         NULL, NULL, NULL,                                                     \
  }

// General purpose registers.
// EH_Frame, Generic, Process Plugin
#define PPC64_REGS                                                             \
  DEFINE_GPR_PPC64(r0, NULL, LLDB_INVALID_REGNUM)                              \
  , DEFINE_GPR_PPC64(r1, NULL, LLDB_REGNUM_GENERIC_SP),                        \
      DEFINE_GPR_PPC64(r2, NULL, LLDB_INVALID_REGNUM),                         \
      DEFINE_GPR_PPC64(r3, NULL, LLDB_REGNUM_GENERIC_ARG1),                    \
      DEFINE_GPR_PPC64(r4, NULL, LLDB_REGNUM_GENERIC_ARG2),                    \
      DEFINE_GPR_PPC64(r5, NULL, LLDB_REGNUM_GENERIC_ARG3),                    \
      DEFINE_GPR_PPC64(r6, NULL, LLDB_REGNUM_GENERIC_ARG4),                    \
      DEFINE_GPR_PPC64(r7, NULL, LLDB_REGNUM_GENERIC_ARG5),                    \
      DEFINE_GPR_PPC64(r8, NULL, LLDB_REGNUM_GENERIC_ARG6),                    \
      DEFINE_GPR_PPC64(r9, NULL, LLDB_REGNUM_GENERIC_ARG7),                    \
      DEFINE_GPR_PPC64(r10, NULL, LLDB_REGNUM_GENERIC_ARG8),                   \
      DEFINE_GPR_PPC64(r11, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r12, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r13, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r14, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r15, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r16, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r17, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r18, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r19, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r20, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r21, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r22, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r23, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r24, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r25, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r26, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r27, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r28, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r29, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r30, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(r31, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(cr, NULL, LLDB_REGNUM_GENERIC_FLAGS),                   \
      DEFINE_GPR_PPC64(msr, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(xer, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(lr, NULL, LLDB_REGNUM_GENERIC_RA),                      \
      DEFINE_GPR_PPC64(ctr, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_GPR_PPC64(pc, NULL, LLDB_REGNUM_GENERIC_PC),                      \
      DEFINE_FPR_PPC64(f0, NULL, LLDB_INVALID_REGNUM),                         \
      DEFINE_FPR_PPC64(f1, NULL, LLDB_INVALID_REGNUM),                         \
      DEFINE_FPR_PPC64(f2, NULL, LLDB_INVALID_REGNUM),                         \
      DEFINE_FPR_PPC64(f3, NULL, LLDB_INVALID_REGNUM),                         \
      DEFINE_FPR_PPC64(f4, NULL, LLDB_INVALID_REGNUM),                         \
      DEFINE_FPR_PPC64(f5, NULL, LLDB_INVALID_REGNUM),                         \
      DEFINE_FPR_PPC64(f6, NULL, LLDB_INVALID_REGNUM),                         \
      DEFINE_FPR_PPC64(f7, NULL, LLDB_INVALID_REGNUM),                         \
      DEFINE_FPR_PPC64(f8, NULL, LLDB_INVALID_REGNUM),                         \
      DEFINE_FPR_PPC64(f9, NULL, LLDB_INVALID_REGNUM),                         \
      DEFINE_FPR_PPC64(f10, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f11, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f12, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f13, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f14, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f15, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f16, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f17, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f18, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f19, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f20, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f21, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f22, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f23, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f24, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f25, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f26, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f27, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f28, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f29, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f30, NULL, LLDB_INVALID_REGNUM),                        \
      DEFINE_FPR_PPC64(f31, NULL, LLDB_INVALID_REGNUM),                        \
      {"fpscr",                                                                \
       NULL,                                                                   \
       8,                                                                      \
       FPR_PPC64_OFFSET(fpscr),                                                \
       lldb::eEncodingUint,                                                    \
       lldb::eFormatHex,                                                       \
       {ppc64_dwarf::dwarf_fpscr_ppc64,                                        \
        ppc64_dwarf::dwarf_fpscr_ppc64, LLDB_INVALID_REGNUM,                   \
        LLDB_INVALID_REGNUM, fpr_fpscr_ppc64},                                 \
       NULL,                                                                   \
       NULL,                                                                   \
       NULL,                                                                   \
       },                                                                      \
      DEFINE_VMX_PPC64(vr0, LLDB_INVALID_REGNUM),                              \
      DEFINE_VMX_PPC64(vr1, LLDB_INVALID_REGNUM),                              \
      DEFINE_VMX_PPC64(vr2, LLDB_INVALID_REGNUM),                              \
      DEFINE_VMX_PPC64(vr3, LLDB_INVALID_REGNUM),                              \
      DEFINE_VMX_PPC64(vr4, LLDB_INVALID_REGNUM),                              \
      DEFINE_VMX_PPC64(vr5, LLDB_INVALID_REGNUM),                              \
      DEFINE_VMX_PPC64(vr6, LLDB_INVALID_REGNUM),                              \
      DEFINE_VMX_PPC64(vr7, LLDB_INVALID_REGNUM),                              \
      DEFINE_VMX_PPC64(vr8, LLDB_INVALID_REGNUM),                              \
      DEFINE_VMX_PPC64(vr9, LLDB_INVALID_REGNUM),                              \
      DEFINE_VMX_PPC64(vr10, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr11, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr12, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr13, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr14, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr15, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr16, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr17, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr18, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr19, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr20, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr21, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr22, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr23, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr24, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr25, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr26, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr27, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr28, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr29, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr30, LLDB_INVALID_REGNUM),                             \
      DEFINE_VMX_PPC64(vr31, LLDB_INVALID_REGNUM),                             \
      {"vscr",                                                                 \
       NULL,                                                                   \
       4,                                                                      \
       VMX_PPC64_OFFSET(vscr),                                                 \
       lldb::eEncodingUint,                                                    \
       lldb::eFormatHex,                                                       \
       {ppc64_dwarf::dwarf_vscr_ppc64, ppc64_dwarf::dwarf_vscr_ppc64,          \
        LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, vmx_vscr_ppc64},             \
       NULL,                                                                   \
       NULL,                                                                   \
       NULL,                                                                   \
       },                                                                      \
      {"vrsave",                                                               \
       NULL,                                                                   \
       4,                                                                      \
       VMX_PPC64_OFFSET(vrsave),                                               \
       lldb::eEncodingUint,                                                    \
       lldb::eFormatHex,                                                       \
       {ppc64_dwarf::dwarf_vrsave_ppc64,                                       \
        ppc64_dwarf::dwarf_vrsave_ppc64, LLDB_INVALID_REGNUM,                  \
        LLDB_INVALID_REGNUM, vmx_vrsave_ppc64},                                \
       NULL,                                                                   \
       NULL,                                                                   \
       NULL,                                                                   \
       },  /* */

typedef struct _GPR_PPC64 {
  uint64_t r0;
  uint64_t r1;
  uint64_t r2;
  uint64_t r3;
  uint64_t r4;
  uint64_t r5;
  uint64_t r6;
  uint64_t r7;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t r16;
  uint64_t r17;
  uint64_t r18;
  uint64_t r19;
  uint64_t r20;
  uint64_t r21;
  uint64_t r22;
  uint64_t r23;
  uint64_t r24;
  uint64_t r25;
  uint64_t r26;
  uint64_t r27;
  uint64_t r28;
  uint64_t r29;
  uint64_t r30;
  uint64_t r31;
  uint64_t cr;
  uint64_t msr;
  uint64_t xer;
  uint64_t lr;
  uint64_t ctr;
  uint64_t pc;
  uint64_t pad[3];
} GPR_PPC64;

typedef struct _FPR_PPC64 {
  uint64_t f0;
  uint64_t f1;
  uint64_t f2;
  uint64_t f3;
  uint64_t f4;
  uint64_t f5;
  uint64_t f6;
  uint64_t f7;
  uint64_t f8;
  uint64_t f9;
  uint64_t f10;
  uint64_t f11;
  uint64_t f12;
  uint64_t f13;
  uint64_t f14;
  uint64_t f15;
  uint64_t f16;
  uint64_t f17;
  uint64_t f18;
  uint64_t f19;
  uint64_t f20;
  uint64_t f21;
  uint64_t f22;
  uint64_t f23;
  uint64_t f24;
  uint64_t f25;
  uint64_t f26;
  uint64_t f27;
  uint64_t f28;
  uint64_t f29;
  uint64_t f30;
  uint64_t f31;
  uint64_t fpscr;
} FPR_PPC64;

typedef struct _VMX_PPC64 {
  uint32_t vr0[4];
  uint32_t vr1[4];
  uint32_t vr2[4];
  uint32_t vr3[4];
  uint32_t vr4[4];
  uint32_t vr5[4];
  uint32_t vr6[4];
  uint32_t vr7[4];
  uint32_t vr8[4];
  uint32_t vr9[4];
  uint32_t vr10[4];
  uint32_t vr11[4];
  uint32_t vr12[4];
  uint32_t vr13[4];
  uint32_t vr14[4];
  uint32_t vr15[4];
  uint32_t vr16[4];
  uint32_t vr17[4];
  uint32_t vr18[4];
  uint32_t vr19[4];
  uint32_t vr20[4];
  uint32_t vr21[4];
  uint32_t vr22[4];
  uint32_t vr23[4];
  uint32_t vr24[4];
  uint32_t vr25[4];
  uint32_t vr26[4];
  uint32_t vr27[4];
  uint32_t vr28[4];
  uint32_t vr29[4];
  uint32_t vr30[4];
  uint32_t vr31[4];
  uint32_t pad[2];
  uint32_t vscr[2];
  uint32_t vrsave;
} VMX_PPC64;


static lldb_private::RegisterInfo g_register_infos_ppc64[] = {
    PPC64_REGS
};

static_assert((sizeof(g_register_infos_ppc64) /
               sizeof(g_register_infos_ppc64[0])) ==
                  k_num_registers_ppc64,
              "g_register_infos_powerpc64 has wrong number of register infos");

#undef DEFINE_FPR_PPC64
#undef DEFINE_GPR_PPC64
#undef DEFINE_VMX_PPC64

#endif // DECLARE_REGISTER_INFOS_PPC64_STRUCT
