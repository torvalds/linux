//===-- RegisterInfos_i386.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Compiler.h"
#include <cstddef>
#include <cstdint>


#ifdef DECLARE_REGISTER_INFOS_I386_STRUCT

// Computes the offset of the given GPR in the user data area.
#define GPR_OFFSET(regname) (LLVM_EXTENSION offsetof(GPR, regname))

// Computes the offset of the given FPR in the extended data area.
#define FPR_OFFSET(regname)                                                    \
  (LLVM_EXTENSION offsetof(UserArea, i387) +                                   \
   LLVM_EXTENSION offsetof(FPR_i386, regname))

// Computes the offset of the YMM register assembled from register halves.
// Based on DNBArchImplI386.cpp from debugserver
#define YMM_OFFSET(reg_index)                                                  \
  (LLVM_EXTENSION offsetof(UserArea, i387) +                                   \
   LLVM_EXTENSION offsetof(FPR, fxsave) +                                      \
   LLVM_EXTENSION offsetof(FXSAVE, xmm[7]) + sizeof(XMMReg) +                  \
   (32 * reg_index))

#define BNDR_OFFSET(reg_index)                                                 \
  (LLVM_EXTENSION offsetof(UserArea, i387) +                                   \
   LLVM_EXTENSION offsetof(FPR, xsave) +                                       \
   LLVM_EXTENSION offsetof(XSAVE, mpxr[reg_index]))

#define BNDC_OFFSET(reg_index)                                                 \
  (LLVM_EXTENSION offsetof(UserArea, i387) +                                   \
   LLVM_EXTENSION offsetof(FPR, xsave) +                                       \
   LLVM_EXTENSION offsetof(XSAVE, mpxc[reg_index]))

// Number of bytes needed to represent a FPR.
#if !defined(FPR_SIZE)
#define FPR_SIZE(reg) sizeof(((FXSAVE *)nullptr)->reg)
#endif

// Number of bytes needed to represent the i'th FP register.
#define FP_SIZE sizeof(((MMSReg *)nullptr)->bytes)

// Number of bytes needed to represent an XMM register.
#define XMM_SIZE sizeof(XMMReg)

// Number of bytes needed to represent a YMM register.
#define YMM_SIZE sizeof(YMMReg)

// Number of bytes needed to represent MPX registers.
#define BNDR_SIZE sizeof(MPXReg)
#define BNDC_SIZE sizeof(MPXCsr)

// Note that the size and offset will be updated by platform-specific classes.
#define DEFINE_GPR(reg, alt, kind1, kind2, kind3, kind4)                       \
  {                                                                            \
    #reg, alt, sizeof(((GPR *)nullptr)->reg),                                  \
                      GPR_OFFSET(reg), eEncodingUint, eFormatHex,              \
                                 {kind1, kind2, kind3, kind4,                  \
                                  lldb_##reg##_i386 },                         \
                                  nullptr, nullptr, nullptr,                   \
  }

#define DEFINE_FPR(name, reg, kind1, kind2, kind3, kind4)                      \
  {                                                                            \
    #name, nullptr, FPR_SIZE(reg), FPR_OFFSET(reg), eEncodingUint, eFormatHex, \
                                           {kind1, kind2, kind3, kind4,        \
                                            lldb_##name##_i386 },              \
                                            nullptr, nullptr, nullptr,         \
  }

// RegisterKind: EHFrame, DWARF, Generic, Process Plugin, LLDB

#define DEFINE_FP_ST(reg, i)                                                   \
  {                                                                            \
    #reg #i, nullptr, FP_SIZE,                                                 \
        LLVM_EXTENSION FPR_OFFSET(                                             \
            stmm[i]), eEncodingVector, eFormatVectorOfUInt8,                   \
            {ehframe_st##i##_i386, dwarf_st##i##_i386, LLDB_INVALID_REGNUM,    \
             LLDB_INVALID_REGNUM, lldb_st##i##_i386 },                         \
             nullptr, nullptr, nullptr,                                        \
  }

#define DEFINE_FP_MM(reg, i, streg)                                            \
  {                                                                            \
    #reg #i, nullptr, sizeof(uint64_t), LLVM_EXTENSION FPR_OFFSET(stmm[i]),    \
    eEncodingUint, eFormatHex,                                                 \
    {dwarf_mm##i##_i386, dwarf_mm##i##_i386, LLDB_INVALID_REGNUM,              \
     LLDB_INVALID_REGNUM, lldb_mm##i##_i386 },                                 \
    RegisterContextPOSIX_x86::g_contained_##streg##_32,                        \
    RegisterContextPOSIX_x86::g_invalidate_##streg##_32,                       \
    nullptr,                                                                   \
  }

#define DEFINE_XMM(reg, i)                                                     \
  {                                                                            \
    #reg #i, nullptr, XMM_SIZE,                                                \
        LLVM_EXTENSION FPR_OFFSET(                                             \
            reg[i]), eEncodingVector, eFormatVectorOfUInt8,                    \
            {ehframe_##reg##i##_i386, dwarf_##reg##i##_i386,                   \
             LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, lldb_##reg##i##_i386 }, \
             nullptr, nullptr, nullptr,                                        \
  }

// I believe the YMM registers use dwarf_xmm_%_i386 register numbers and then
// differentiate based on register size.
#define DEFINE_YMM(reg, i)                                                     \
  {                                                                            \
    #reg #i, nullptr, YMM_SIZE,                                                \
        LLVM_EXTENSION YMM_OFFSET(i), eEncodingVector, eFormatVectorOfUInt8,   \
                                  {LLDB_INVALID_REGNUM, dwarf_xmm##i##_i386,   \
                                   LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,   \
                                   lldb_##reg##i##_i386 },                     \
                                   nullptr, nullptr, nullptr,                  \
  }

#define DEFINE_BNDR(reg, i)                                                    \
  {                                                                            \
    #reg #i, nullptr, BNDR_SIZE,                                               \
        LLVM_EXTENSION BNDR_OFFSET(i), eEncodingVector, eFormatVectorOfUInt64, \
        {dwarf_##reg##i##_i386, dwarf_##reg##i##_i386, LLDB_INVALID_REGNUM,    \
         LLDB_INVALID_REGNUM, lldb_##reg##i##_i386 },                          \
         nullptr, nullptr, nullptr,                                            \
  }

#define DEFINE_BNDC(name, i)                                                   \
  {                                                                            \
    #name, nullptr, BNDC_SIZE,                                                 \
           LLVM_EXTENSION BNDC_OFFSET(i), eEncodingVector,                     \
           eFormatVectorOfUInt8,                                               \
           {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,     \
            LLDB_INVALID_REGNUM, lldb_##name##_i386 },                         \
            nullptr, nullptr, nullptr,                                         \
  }

#define DEFINE_DR(reg, i)                                                      \
  {                                                                            \
    #reg #i, nullptr, DR_SIZE,                                                 \
        DR_OFFSET(i), eEncodingUint, eFormatHex,                               \
                  {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                   \
                   LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                   \
                   lldb_##reg##i##_i386 },                                     \
                   nullptr, nullptr, nullptr,                                  \
  }

#define DEFINE_GPR_PSEUDO_16(reg16, reg32)                                     \
  {                                                                            \
    #reg16, nullptr, 2,                                                        \
        GPR_OFFSET(reg32), eEncodingUint, eFormatHex,                          \
                   {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    lldb_##reg16##_i386 },                                     \
                    RegisterContextPOSIX_x86::g_contained_##reg32,             \
                    RegisterContextPOSIX_x86::g_invalidate_##reg32,            \
                    nullptr,                                                   \
  }

#define DEFINE_GPR_PSEUDO_8H(reg8, reg32)                                      \
  {                                                                            \
    #reg8, nullptr, 1,                                                         \
        GPR_OFFSET(reg32) + 1, eEncodingUint, eFormatHex,                      \
                   {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    lldb_##reg8##_i386 },                                      \
                    RegisterContextPOSIX_x86::g_contained_##reg32,             \
                    RegisterContextPOSIX_x86::g_invalidate_##reg32,            \
                    nullptr,                                                   \
  }

#define DEFINE_GPR_PSEUDO_8L(reg8, reg32)                                      \
  {                                                                            \
    #reg8, nullptr, 1,                                                         \
        GPR_OFFSET(reg32), eEncodingUint, eFormatHex,                          \
                   {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    lldb_##reg8##_i386 },                                      \
                    RegisterContextPOSIX_x86::g_contained_##reg32,             \
                    RegisterContextPOSIX_x86::g_invalidate_##reg32,            \
                    nullptr,                                                   \
  }

static RegisterInfo g_register_infos_i386[] = {
    // General purpose registers.
    DEFINE_GPR(eax, nullptr, ehframe_eax_i386, dwarf_eax_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(ebx, nullptr, ehframe_ebx_i386, dwarf_ebx_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(ecx, nullptr, ehframe_ecx_i386, dwarf_ecx_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(edx, nullptr, ehframe_edx_i386, dwarf_edx_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(edi, nullptr, ehframe_edi_i386, dwarf_edi_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(esi, nullptr, ehframe_esi_i386, dwarf_esi_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(ebp, "fp", ehframe_ebp_i386, dwarf_ebp_i386,
               LLDB_REGNUM_GENERIC_FP, LLDB_INVALID_REGNUM),
    DEFINE_GPR(esp, "sp", ehframe_esp_i386, dwarf_esp_i386,
               LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM),
    DEFINE_GPR(eip, "pc", ehframe_eip_i386, dwarf_eip_i386,
               LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM),
    DEFINE_GPR(eflags, "flags", ehframe_eflags_i386, dwarf_eflags_i386,
               LLDB_REGNUM_GENERIC_FLAGS, LLDB_INVALID_REGNUM),
    DEFINE_GPR(cs, nullptr, LLDB_INVALID_REGNUM, dwarf_cs_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(fs, nullptr, LLDB_INVALID_REGNUM, dwarf_fs_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(gs, nullptr, LLDB_INVALID_REGNUM, dwarf_gs_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(ss, nullptr, LLDB_INVALID_REGNUM, dwarf_ss_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(ds, nullptr, LLDB_INVALID_REGNUM, dwarf_ds_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(es, nullptr, LLDB_INVALID_REGNUM, dwarf_es_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),

    DEFINE_GPR_PSEUDO_16(ax, eax), DEFINE_GPR_PSEUDO_16(bx, ebx),
    DEFINE_GPR_PSEUDO_16(cx, ecx), DEFINE_GPR_PSEUDO_16(dx, edx),
    DEFINE_GPR_PSEUDO_16(di, edi), DEFINE_GPR_PSEUDO_16(si, esi),
    DEFINE_GPR_PSEUDO_16(bp, ebp), DEFINE_GPR_PSEUDO_16(sp, esp),
    DEFINE_GPR_PSEUDO_8H(ah, eax), DEFINE_GPR_PSEUDO_8H(bh, ebx),
    DEFINE_GPR_PSEUDO_8H(ch, ecx), DEFINE_GPR_PSEUDO_8H(dh, edx),
    DEFINE_GPR_PSEUDO_8L(al, eax), DEFINE_GPR_PSEUDO_8L(bl, ebx),
    DEFINE_GPR_PSEUDO_8L(cl, ecx), DEFINE_GPR_PSEUDO_8L(dl, edx),

    // i387 Floating point registers.
    DEFINE_FPR(fctrl, fctrl, LLDB_INVALID_REGNUM, dwarf_fctrl_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(fstat, fstat, LLDB_INVALID_REGNUM, dwarf_fstat_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(ftag, ftag, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(fop, fop, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(fiseg, ptr.i386_.fiseg, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(fioff, ptr.i386_.fioff, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(foseg, ptr.i386_.foseg, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(fooff, ptr.i386_.fooff, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(mxcsr, mxcsr, LLDB_INVALID_REGNUM, dwarf_mxcsr_i386,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(mxcsrmask, mxcsrmask, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),

    // FP registers.
    DEFINE_FP_ST(st, 0), DEFINE_FP_ST(st, 1), DEFINE_FP_ST(st, 2),
    DEFINE_FP_ST(st, 3), DEFINE_FP_ST(st, 4), DEFINE_FP_ST(st, 5),
    DEFINE_FP_ST(st, 6), DEFINE_FP_ST(st, 7),

    DEFINE_FP_MM(mm, 0, st0), DEFINE_FP_MM(mm, 1, st1),
    DEFINE_FP_MM(mm, 2, st2), DEFINE_FP_MM(mm, 3, st3),
    DEFINE_FP_MM(mm, 4, st4), DEFINE_FP_MM(mm, 5, st5),
    DEFINE_FP_MM(mm, 6, st6), DEFINE_FP_MM(mm, 7, st7),

    // XMM registers
    DEFINE_XMM(xmm, 0), DEFINE_XMM(xmm, 1), DEFINE_XMM(xmm, 2),
    DEFINE_XMM(xmm, 3), DEFINE_XMM(xmm, 4), DEFINE_XMM(xmm, 5),
    DEFINE_XMM(xmm, 6), DEFINE_XMM(xmm, 7),

    // Copy of YMM registers assembled from xmm and ymmh
    DEFINE_YMM(ymm, 0), DEFINE_YMM(ymm, 1), DEFINE_YMM(ymm, 2),
    DEFINE_YMM(ymm, 3), DEFINE_YMM(ymm, 4), DEFINE_YMM(ymm, 5),
    DEFINE_YMM(ymm, 6), DEFINE_YMM(ymm, 7),

    // MPX registers
    DEFINE_BNDR(bnd, 0),
    DEFINE_BNDR(bnd, 1),
    DEFINE_BNDR(bnd, 2),
    DEFINE_BNDR(bnd, 3),

    DEFINE_BNDC(bndcfgu, 0),
    DEFINE_BNDC(bndstatus, 1),

    // Debug registers for lldb internal use
    DEFINE_DR(dr, 0), DEFINE_DR(dr, 1), DEFINE_DR(dr, 2), DEFINE_DR(dr, 3),
    DEFINE_DR(dr, 4), DEFINE_DR(dr, 5), DEFINE_DR(dr, 6), DEFINE_DR(dr, 7)};

static_assert((sizeof(g_register_infos_i386) /
               sizeof(g_register_infos_i386[0])) == k_num_registers_i386,
              "g_register_infos_x86_64 has wrong number of register infos");

#undef GPR_OFFSET
#undef FPR_OFFSET
#undef YMM_OFFSET
#undef FPR_SIZE
#undef FP_SIZE
#undef XMM_SIZE
#undef YMM_SIZE
#undef DEFINE_GPR
#undef DEFINE_FPR
#undef DEFINE_FP
#undef DEFINE_XMM
#undef DEFINE_YMM
#undef DEFINE_BNDR
#undef DEFINE_BNDC
#undef DEFINE_DR
#undef DEFINE_GPR_PSEUDO_16
#undef DEFINE_GPR_PSEUDO_8H
#undef DEFINE_GPR_PSEUDO_8L

#endif // DECLARE_REGISTER_INFOS_I386_STRUCT
