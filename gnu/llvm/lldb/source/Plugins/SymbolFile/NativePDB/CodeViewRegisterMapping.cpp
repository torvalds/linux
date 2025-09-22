//===-- CodeViewRegisterMapping.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CodeViewRegisterMapping.h"

#include "lldb/lldb-defines.h"

#include "Plugins/Process/Utility/lldb-arm64-register-enums.h"
#include "Plugins/Process/Utility/lldb-x86-register-enums.h"

using namespace lldb_private;

static const uint32_t g_code_view_to_lldb_registers_arm64[] = {
    LLDB_INVALID_REGNUM, // NONE
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    gpr_w0_arm64,  // ARM64_W0, 10)
    gpr_w1_arm64,  // ARM64_W1, 11)
    gpr_w2_arm64,  // ARM64_W2, 12)
    gpr_w3_arm64,  // ARM64_W3, 13)
    gpr_w4_arm64,  // ARM64_W4, 14)
    gpr_w5_arm64,  // ARM64_W5, 15)
    gpr_w6_arm64,  // ARM64_W6, 16)
    gpr_w7_arm64,  // ARM64_W7, 17)
    gpr_w8_arm64,  // ARM64_W8, 18)
    gpr_w9_arm64,  // ARM64_W9, 19)
    gpr_w10_arm64, // ARM64_W10, 20)
    gpr_w11_arm64, // ARM64_W11, 21)
    gpr_w12_arm64, // ARM64_W12, 22)
    gpr_w13_arm64, // ARM64_W13, 23)
    gpr_w14_arm64, // ARM64_W14, 24)
    gpr_w15_arm64, // ARM64_W15, 25)
    gpr_w16_arm64, // ARM64_W16, 26)
    gpr_w17_arm64, // ARM64_W17, 27)
    gpr_w18_arm64, // ARM64_W18, 28)
    gpr_w19_arm64, // ARM64_W19, 29)
    gpr_w20_arm64, // ARM64_W20, 30)
    gpr_w21_arm64, // ARM64_W21, 31)
    gpr_w22_arm64, // ARM64_W22, 32)
    gpr_w23_arm64, // ARM64_W23, 33)
    gpr_w24_arm64, // ARM64_W24, 34)
    gpr_w25_arm64, // ARM64_W25, 35)
    gpr_w26_arm64, // ARM64_W26, 36)
    gpr_w27_arm64, // ARM64_W27, 37)
    gpr_w28_arm64, // ARM64_W28, 38)
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    gpr_x0_arm64,  // ARM64_X0, 50)
    gpr_x1_arm64,  // ARM64_X1, 51)
    gpr_x2_arm64,  // ARM64_X2, 52)
    gpr_x3_arm64,  // ARM64_X3, 53)
    gpr_x4_arm64,  // ARM64_X4, 54)
    gpr_x5_arm64,  // ARM64_X5, 55)
    gpr_x6_arm64,  // ARM64_X6, 56)
    gpr_x7_arm64,  // ARM64_X7, 57)
    gpr_x8_arm64,  // ARM64_X8, 58)
    gpr_x9_arm64,  // ARM64_X9, 59)
    gpr_x10_arm64, // ARM64_X10, 60)
    gpr_x11_arm64, // ARM64_X11, 61)
    gpr_x12_arm64, // ARM64_X12, 62)
    gpr_x13_arm64, // ARM64_X13, 63)
    gpr_x14_arm64, // ARM64_X14, 64)
    gpr_x15_arm64, // ARM64_X15, 65)
    gpr_x16_arm64, // ARM64_X16, 66)
    gpr_x17_arm64, // ARM64_X17, 67)
    gpr_x18_arm64, // ARM64_X18, 68)
    gpr_x19_arm64, // ARM64_X19, 69)
    gpr_x20_arm64, // ARM64_X20, 70)
    gpr_x21_arm64, // ARM64_X21, 71)
    gpr_x22_arm64, // ARM64_X22, 72)
    gpr_x23_arm64, // ARM64_X23, 73)
    gpr_x24_arm64, // ARM64_X24, 74)
    gpr_x25_arm64, // ARM64_X25, 75)
    gpr_x26_arm64, // ARM64_X26, 76)
    gpr_x27_arm64, // ARM64_X27, 77)
    gpr_x28_arm64, // ARM64_X28, 78)
    gpr_fp_arm64,  // ARM64_FP, 79)
    gpr_lr_arm64,  // ARM64_LR, 80)
    gpr_sp_arm64,  // ARM64_SP, 81)
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    gpr_cpsr_arm64, // ARM64_NZCV, 90)
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    fpu_s0_arm64,  // (ARM64_S0, 100)
    fpu_s1_arm64,  // (ARM64_S1, 101)
    fpu_s2_arm64,  // (ARM64_S2, 102)
    fpu_s3_arm64,  // (ARM64_S3, 103)
    fpu_s4_arm64,  // (ARM64_S4, 104)
    fpu_s5_arm64,  // (ARM64_S5, 105)
    fpu_s6_arm64,  // (ARM64_S6, 106)
    fpu_s7_arm64,  // (ARM64_S7, 107)
    fpu_s8_arm64,  // (ARM64_S8, 108)
    fpu_s9_arm64,  // (ARM64_S9, 109)
    fpu_s10_arm64, // (ARM64_S10, 110)
    fpu_s11_arm64, // (ARM64_S11, 111)
    fpu_s12_arm64, // (ARM64_S12, 112)
    fpu_s13_arm64, // (ARM64_S13, 113)
    fpu_s14_arm64, // (ARM64_S14, 114)
    fpu_s15_arm64, // (ARM64_S15, 115)
    fpu_s16_arm64, // (ARM64_S16, 116)
    fpu_s17_arm64, // (ARM64_S17, 117)
    fpu_s18_arm64, // (ARM64_S18, 118)
    fpu_s19_arm64, // (ARM64_S19, 119)
    fpu_s20_arm64, // (ARM64_S20, 120)
    fpu_s21_arm64, // (ARM64_S21, 121)
    fpu_s22_arm64, // (ARM64_S22, 122)
    fpu_s23_arm64, // (ARM64_S23, 123)
    fpu_s24_arm64, // (ARM64_S24, 124)
    fpu_s25_arm64, // (ARM64_S25, 125)
    fpu_s26_arm64, // (ARM64_S26, 126)
    fpu_s27_arm64, // (ARM64_S27, 127)
    fpu_s28_arm64, // (ARM64_S28, 128)
    fpu_s29_arm64, // (ARM64_S29, 129)
    fpu_s30_arm64, // (ARM64_S30, 130)
    fpu_s31_arm64, // (ARM64_S31, 131)
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    fpu_d0_arm64,  // (ARM64_D0, 140)
    fpu_d1_arm64,  // (ARM64_D1, 141)
    fpu_d2_arm64,  // (ARM64_D2, 142)
    fpu_d3_arm64,  // (ARM64_D3, 143)
    fpu_d4_arm64,  // (ARM64_D4, 144)
    fpu_d5_arm64,  // (ARM64_D5, 145)
    fpu_d6_arm64,  // (ARM64_D6, 146)
    fpu_d7_arm64,  // (ARM64_D7, 147)
    fpu_d8_arm64,  // (ARM64_D8, 148)
    fpu_d9_arm64,  // (ARM64_D9, 149)
    fpu_d10_arm64, // (ARM64_D10, 150)
    fpu_d11_arm64, // (ARM64_D11, 151)
    fpu_d12_arm64, // (ARM64_D12, 152)
    fpu_d13_arm64, // (ARM64_D13, 153)
    fpu_d14_arm64, // (ARM64_D14, 154)
    fpu_d15_arm64, // (ARM64_D15, 155)
    fpu_d16_arm64, // (ARM64_D16, 156)
    fpu_d17_arm64, // (ARM64_D17, 157)
    fpu_d18_arm64, // (ARM64_D18, 158)
    fpu_d19_arm64, // (ARM64_D19, 159)
    fpu_d20_arm64, // (ARM64_D20, 160)
    fpu_d21_arm64, // (ARM64_D21, 161)
    fpu_d22_arm64, // (ARM64_D22, 162)
    fpu_d23_arm64, // (ARM64_D23, 163)
    fpu_d24_arm64, // (ARM64_D24, 164)
    fpu_d25_arm64, // (ARM64_D25, 165)
    fpu_d26_arm64, // (ARM64_D26, 166)
    fpu_d27_arm64, // (ARM64_D27, 167)
    fpu_d28_arm64, // (ARM64_D28, 168)
    fpu_d29_arm64, // (ARM64_D29, 169)
    fpu_d30_arm64, // (ARM64_D30, 170)
    fpu_d31_arm64, // (ARM64_D31, 171)
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    fpu_v0_arm64,  // (ARM64_Q0, 180)
    fpu_v1_arm64,  // (ARM64_Q1, 181)
    fpu_v2_arm64,  // (ARM64_Q2, 182)
    fpu_v3_arm64,  // (ARM64_Q3, 183)
    fpu_v4_arm64,  // (ARM64_Q4, 184)
    fpu_v5_arm64,  // (ARM64_Q5, 185)
    fpu_v6_arm64,  // (ARM64_Q6, 186)
    fpu_v7_arm64,  // (ARM64_Q7, 187)
    fpu_v8_arm64,  // (ARM64_Q8, 188)
    fpu_v9_arm64,  // (ARM64_Q9, 189)
    fpu_v10_arm64, // (ARM64_Q10, 190)
    fpu_v11_arm64, // (ARM64_Q11, 191)
    fpu_v12_arm64, // (ARM64_Q12, 192)
    fpu_v13_arm64, // (ARM64_Q13, 193)
    fpu_v14_arm64, // (ARM64_Q14, 194)
    fpu_v15_arm64, // (ARM64_Q15, 195)
    fpu_v16_arm64, // (ARM64_Q16, 196)
    fpu_v17_arm64, // (ARM64_Q17, 197)
    fpu_v18_arm64, // (ARM64_Q18, 198)
    fpu_v19_arm64, // (ARM64_Q19, 199)
    fpu_v20_arm64, // (ARM64_Q20, 200)
    fpu_v21_arm64, // (ARM64_Q21, 201)
    fpu_v22_arm64, // (ARM64_Q22, 202)
    fpu_v23_arm64, // (ARM64_Q23, 203)
    fpu_v24_arm64, // (ARM64_Q24, 204)
    fpu_v25_arm64, // (ARM64_Q25, 205)
    fpu_v26_arm64, // (ARM64_Q26, 206)
    fpu_v27_arm64, // (ARM64_Q27, 207)
    fpu_v28_arm64, // (ARM64_Q28, 208)
    fpu_v29_arm64, // (ARM64_Q29, 209)
    fpu_v30_arm64, // (ARM64_Q30, 210)
    fpu_v31_arm64, // (ARM64_Q31, 211)
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    fpu_fpsr_arm64 // ARM64_FPSR, 220)
};

static const uint32_t g_code_view_to_lldb_registers_x86[] = {
    LLDB_INVALID_REGNUM, // NONE
    lldb_al_i386,        // AL
    lldb_cl_i386,        // CL
    lldb_dl_i386,        // DL
    lldb_bl_i386,        // BL
    lldb_ah_i386,        // AH
    lldb_ch_i386,        // CH
    lldb_dh_i386,        // DH
    lldb_bh_i386,        // BH
    lldb_ax_i386,        // AX
    lldb_cx_i386,        // CX
    lldb_dx_i386,        // DX
    lldb_bx_i386,        // BX
    lldb_sp_i386,        // SP
    lldb_bp_i386,        // BP
    lldb_si_i386,        // SI
    lldb_di_i386,        // DI
    lldb_eax_i386,       // EAX
    lldb_ecx_i386,       // ECX
    lldb_edx_i386,       // EDX
    lldb_ebx_i386,       // EBX
    lldb_esp_i386,       // ESP
    lldb_ebp_i386,       // EBP
    lldb_esi_i386,       // ESI
    lldb_edi_i386,       // EDI
    lldb_es_i386,        // ES
    lldb_cs_i386,        // CS
    lldb_ss_i386,        // SS
    lldb_ds_i386,        // DS
    lldb_fs_i386,        // FS
    lldb_gs_i386,        // GS
    LLDB_INVALID_REGNUM, // IP
    LLDB_INVALID_REGNUM, // FLAGS
    lldb_eip_i386,       // EIP
    lldb_eflags_i386,    // EFLAGS
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, // TEMP
    LLDB_INVALID_REGNUM, // TEMPH
    LLDB_INVALID_REGNUM, // QUOTE
    LLDB_INVALID_REGNUM, // PCDR3
    LLDB_INVALID_REGNUM, // PCDR4
    LLDB_INVALID_REGNUM, // PCDR5
    LLDB_INVALID_REGNUM, // PCDR6
    LLDB_INVALID_REGNUM, // PCDR7
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, // CR0
    LLDB_INVALID_REGNUM, // CR1
    LLDB_INVALID_REGNUM, // CR2
    LLDB_INVALID_REGNUM, // CR3
    LLDB_INVALID_REGNUM, // CR4
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    lldb_dr0_i386, // DR0
    lldb_dr1_i386, // DR1
    lldb_dr2_i386, // DR2
    lldb_dr3_i386, // DR3
    lldb_dr4_i386, // DR4
    lldb_dr5_i386, // DR5
    lldb_dr6_i386, // DR6
    lldb_dr7_i386, // DR7
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, // GDTR
    LLDB_INVALID_REGNUM, // GDTL
    LLDB_INVALID_REGNUM, // IDTR
    LLDB_INVALID_REGNUM, // IDTL
    LLDB_INVALID_REGNUM, // LDTR
    LLDB_INVALID_REGNUM, // TR
    LLDB_INVALID_REGNUM, // PSEUDO1
    LLDB_INVALID_REGNUM, // PSEUDO2
    LLDB_INVALID_REGNUM, // PSEUDO3
    LLDB_INVALID_REGNUM, // PSEUDO4
    LLDB_INVALID_REGNUM, // PSEUDO5
    LLDB_INVALID_REGNUM, // PSEUDO6
    LLDB_INVALID_REGNUM, // PSEUDO7
    LLDB_INVALID_REGNUM, // PSEUDO8
    LLDB_INVALID_REGNUM, // PSEUDO9
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    lldb_st0_i386,       // ST0
    lldb_st1_i386,       // ST1
    lldb_st2_i386,       // ST2
    lldb_st3_i386,       // ST3
    lldb_st4_i386,       // ST4
    lldb_st5_i386,       // ST5
    lldb_st6_i386,       // ST6
    lldb_st7_i386,       // ST7
    LLDB_INVALID_REGNUM, // CTRL
    LLDB_INVALID_REGNUM, // STAT
    LLDB_INVALID_REGNUM, // TAG
    LLDB_INVALID_REGNUM, // FPIP
    LLDB_INVALID_REGNUM, // FPCS
    LLDB_INVALID_REGNUM, // FPDO
    LLDB_INVALID_REGNUM, // FPDS
    LLDB_INVALID_REGNUM, // ISEM
    LLDB_INVALID_REGNUM, // FPEIP
    LLDB_INVALID_REGNUM, // FPEDO
    lldb_mm0_i386,       // MM0
    lldb_mm1_i386,       // MM1
    lldb_mm2_i386,       // MM2
    lldb_mm3_i386,       // MM3
    lldb_mm4_i386,       // MM4
    lldb_mm5_i386,       // MM5
    lldb_mm6_i386,       // MM6
    lldb_mm7_i386,       // MM7
    lldb_xmm0_i386,      // XMM0
    lldb_xmm1_i386,      // XMM1
    lldb_xmm2_i386,      // XMM2
    lldb_xmm3_i386,      // XMM3
    lldb_xmm4_i386,      // XMM4
    lldb_xmm5_i386,      // XMM5
    lldb_xmm6_i386,      // XMM6
    lldb_xmm7_i386       // XMM7
};

static const uint32_t g_code_view_to_lldb_registers_x86_64[] = {
    LLDB_INVALID_REGNUM, // NONE
    lldb_al_x86_64,      // AL
    lldb_cl_x86_64,      // CL
    lldb_dl_x86_64,      // DL
    lldb_bl_x86_64,      // BL
    lldb_ah_x86_64,      // AH
    lldb_ch_x86_64,      // CH
    lldb_dh_x86_64,      // DH
    lldb_bh_x86_64,      // BH
    lldb_ax_x86_64,      // AX
    lldb_cx_x86_64,      // CX
    lldb_dx_x86_64,      // DX
    lldb_bx_x86_64,      // BX
    lldb_sp_x86_64,      // SP
    lldb_bp_x86_64,      // BP
    lldb_si_x86_64,      // SI
    lldb_di_x86_64,      // DI
    lldb_eax_x86_64,     // EAX
    lldb_ecx_x86_64,     // ECX
    lldb_edx_x86_64,     // EDX
    lldb_ebx_x86_64,     // EBX
    lldb_esp_x86_64,     // ESP
    lldb_ebp_x86_64,     // EBP
    lldb_esi_x86_64,     // ESI
    lldb_edi_x86_64,     // EDI
    lldb_es_x86_64,      // ES
    lldb_cs_x86_64,      // CS
    lldb_ss_x86_64,      // SS
    lldb_ds_x86_64,      // DS
    lldb_fs_x86_64,      // FS
    lldb_gs_x86_64,      // GS
    LLDB_INVALID_REGNUM, // IP
    LLDB_INVALID_REGNUM, // FLAGS
    LLDB_INVALID_REGNUM, // EIP
    LLDB_INVALID_REGNUM, // EFLAGS
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, // TEMP
    LLDB_INVALID_REGNUM, // TEMPH
    LLDB_INVALID_REGNUM, // QUOTE
    LLDB_INVALID_REGNUM, // PCDR3
    LLDB_INVALID_REGNUM, // PCDR4
    LLDB_INVALID_REGNUM, // PCDR5
    LLDB_INVALID_REGNUM, // PCDR6
    LLDB_INVALID_REGNUM, // PCDR7
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, // CR0
    LLDB_INVALID_REGNUM, // CR1
    LLDB_INVALID_REGNUM, // CR2
    LLDB_INVALID_REGNUM, // CR3
    LLDB_INVALID_REGNUM, // CR4
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    lldb_dr0_x86_64, // DR0
    lldb_dr1_x86_64, // DR1
    lldb_dr2_x86_64, // DR2
    lldb_dr3_x86_64, // DR3
    lldb_dr4_x86_64, // DR4
    lldb_dr5_x86_64, // DR5
    lldb_dr6_x86_64, // DR6
    lldb_dr7_x86_64, // DR7
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, // GDTR
    LLDB_INVALID_REGNUM, // GDTL
    LLDB_INVALID_REGNUM, // IDTR
    LLDB_INVALID_REGNUM, // IDTL
    LLDB_INVALID_REGNUM, // LDTR
    LLDB_INVALID_REGNUM, // TR
    LLDB_INVALID_REGNUM, // PSEUDO1
    LLDB_INVALID_REGNUM, // PSEUDO2
    LLDB_INVALID_REGNUM, // PSEUDO3
    LLDB_INVALID_REGNUM, // PSEUDO4
    LLDB_INVALID_REGNUM, // PSEUDO5
    LLDB_INVALID_REGNUM, // PSEUDO6
    LLDB_INVALID_REGNUM, // PSEUDO7
    LLDB_INVALID_REGNUM, // PSEUDO8
    LLDB_INVALID_REGNUM, // PSEUDO9
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    lldb_st0_x86_64,     // ST0
    lldb_st1_x86_64,     // ST1
    lldb_st2_x86_64,     // ST2
    lldb_st3_x86_64,     // ST3
    lldb_st4_x86_64,     // ST4
    lldb_st5_x86_64,     // ST5
    lldb_st6_x86_64,     // ST6
    lldb_st7_x86_64,     // ST7
    LLDB_INVALID_REGNUM, // CTRL
    LLDB_INVALID_REGNUM, // STAT
    LLDB_INVALID_REGNUM, // TAG
    LLDB_INVALID_REGNUM, // FPIP
    LLDB_INVALID_REGNUM, // FPCS
    LLDB_INVALID_REGNUM, // FPDO
    LLDB_INVALID_REGNUM, // FPDS
    LLDB_INVALID_REGNUM, // ISEM
    LLDB_INVALID_REGNUM, // FPEIP
    LLDB_INVALID_REGNUM, // FPEDO
    lldb_mm0_x86_64,     // MM0
    lldb_mm1_x86_64,     // MM1
    lldb_mm2_x86_64,     // MM2
    lldb_mm3_x86_64,     // MM3
    lldb_mm4_x86_64,     // MM4
    lldb_mm5_x86_64,     // MM5
    lldb_mm6_x86_64,     // MM6
    lldb_mm7_x86_64,     // MM7
    lldb_xmm0_x86_64,    // XMM0
    lldb_xmm1_x86_64,    // XMM1
    lldb_xmm2_x86_64,    // XMM2
    lldb_xmm3_x86_64,    // XMM3
    lldb_xmm4_x86_64,    // XMM4
    lldb_xmm5_x86_64,    // XMM5
    lldb_xmm6_x86_64,    // XMM6
    lldb_xmm7_x86_64,    // XMM7
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM,
    lldb_mxcsr_x86_64,   // MXCSR
    LLDB_INVALID_REGNUM, // EDXEAX
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, // EMM0L
    LLDB_INVALID_REGNUM, // EMM1L
    LLDB_INVALID_REGNUM, // EMM2L
    LLDB_INVALID_REGNUM, // EMM3L
    LLDB_INVALID_REGNUM, // EMM4L
    LLDB_INVALID_REGNUM, // EMM5L
    LLDB_INVALID_REGNUM, // EMM6L
    LLDB_INVALID_REGNUM, // EMM7L
    LLDB_INVALID_REGNUM, // EMM0H
    LLDB_INVALID_REGNUM, // EMM1H
    LLDB_INVALID_REGNUM, // EMM2H
    LLDB_INVALID_REGNUM, // EMM3H
    LLDB_INVALID_REGNUM, // EMM4H
    LLDB_INVALID_REGNUM, // EMM5H
    LLDB_INVALID_REGNUM, // EMM6H
    LLDB_INVALID_REGNUM, // EMM7H
    LLDB_INVALID_REGNUM, // MM00
    LLDB_INVALID_REGNUM, // MM01
    LLDB_INVALID_REGNUM, // MM10
    LLDB_INVALID_REGNUM, // MM11
    LLDB_INVALID_REGNUM, // MM20
    LLDB_INVALID_REGNUM, // MM21
    LLDB_INVALID_REGNUM, // MM30
    LLDB_INVALID_REGNUM, // MM31
    LLDB_INVALID_REGNUM, // MM40
    LLDB_INVALID_REGNUM, // MM41
    LLDB_INVALID_REGNUM, // MM50
    LLDB_INVALID_REGNUM, // MM51
    LLDB_INVALID_REGNUM, // MM60
    LLDB_INVALID_REGNUM, // MM61
    LLDB_INVALID_REGNUM, // MM70
    LLDB_INVALID_REGNUM, // MM71
    lldb_xmm8_x86_64,    // XMM8
    lldb_xmm9_x86_64,    // XMM9
    lldb_xmm10_x86_64,   // XMM10
    lldb_xmm11_x86_64,   // XMM11
    lldb_xmm12_x86_64,   // XMM12
    lldb_xmm13_x86_64,   // XMM13
    lldb_xmm14_x86_64,   // XMM14
    lldb_xmm15_x86_64,   // XMM15
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM,
    lldb_sil_x86_64,   // SIL
    lldb_dil_x86_64,   // DIL
    lldb_bpl_x86_64,   // BPL
    lldb_spl_x86_64,   // SPL
    lldb_rax_x86_64,   // RAX
    lldb_rbx_x86_64,   // RBX
    lldb_rcx_x86_64,   // RCX
    lldb_rdx_x86_64,   // RDX
    lldb_rsi_x86_64,   // RSI
    lldb_rdi_x86_64,   // RDI
    lldb_rbp_x86_64,   // RBP
    lldb_rsp_x86_64,   // RSP
    lldb_r8_x86_64,    // R8
    lldb_r9_x86_64,    // R9
    lldb_r10_x86_64,   // R10
    lldb_r11_x86_64,   // R11
    lldb_r12_x86_64,   // R12
    lldb_r13_x86_64,   // R13
    lldb_r14_x86_64,   // R14
    lldb_r15_x86_64,   // R15
    lldb_r8l_x86_64,   // R8B
    lldb_r9l_x86_64,   // R9B
    lldb_r10l_x86_64,  // R10B
    lldb_r11l_x86_64,  // R11B
    lldb_r12l_x86_64,  // R12B
    lldb_r13l_x86_64,  // R13B
    lldb_r14l_x86_64,  // R14B
    lldb_r15l_x86_64,  // R15B
    lldb_r8w_x86_64,   // R8W
    lldb_r9w_x86_64,   // R9W
    lldb_r10w_x86_64,  // R10W
    lldb_r11w_x86_64,  // R11W
    lldb_r12w_x86_64,  // R12W
    lldb_r13w_x86_64,  // R13W
    lldb_r14w_x86_64,  // R14W
    lldb_r15w_x86_64,  // R15W
    lldb_r8d_x86_64,   // R8D
    lldb_r9d_x86_64,   // R9D
    lldb_r10d_x86_64,  // R10D
    lldb_r11d_x86_64,  // R11D
    lldb_r12d_x86_64,  // R12D
    lldb_r13d_x86_64,  // R13D
    lldb_r14d_x86_64,  // R14D
    lldb_r15d_x86_64,  // R15D
    lldb_ymm0_x86_64,  // AMD64_YMM0
    lldb_ymm1_x86_64,  // AMD64_YMM1
    lldb_ymm2_x86_64,  // AMD64_YMM2
    lldb_ymm3_x86_64,  // AMD64_YMM3
    lldb_ymm4_x86_64,  // AMD64_YMM4
    lldb_ymm5_x86_64,  // AMD64_YMM5
    lldb_ymm6_x86_64,  // AMD64_YMM6
    lldb_ymm7_x86_64,  // AMD64_YMM7
    lldb_ymm8_x86_64,  // AMD64_YMM8
    lldb_ymm9_x86_64,  // AMD64_YMM9
    lldb_ymm10_x86_64, // AMD64_YMM10
    lldb_ymm11_x86_64, // AMD64_YMM11
    lldb_ymm12_x86_64, // AMD64_YMM12
    lldb_ymm13_x86_64, // AMD64_YMM13
    lldb_ymm14_x86_64, // AMD64_YMM14
    lldb_ymm15_x86_64, // AMD64_YMM15
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
    lldb_bnd0_x86_64, // BND0
    lldb_bnd1_x86_64, // BND1
    lldb_bnd2_x86_64  // BND2
};

uint32_t lldb_private::npdb::GetLLDBRegisterNumber(
    llvm::Triple::ArchType arch_type, llvm::codeview::RegisterId register_id) {
  switch (arch_type) {
  case llvm::Triple::aarch64:
    if (static_cast<uint16_t>(register_id) <
        sizeof(g_code_view_to_lldb_registers_arm64) /
            sizeof(g_code_view_to_lldb_registers_arm64[0]))
      return g_code_view_to_lldb_registers_arm64[static_cast<uint16_t>(
          register_id)];

    return LLDB_INVALID_REGNUM;
  case llvm::Triple::x86:
    if (static_cast<uint16_t>(register_id) <
        sizeof(g_code_view_to_lldb_registers_x86) /
            sizeof(g_code_view_to_lldb_registers_x86[0]))
      return g_code_view_to_lldb_registers_x86[static_cast<uint16_t>(
          register_id)];

    switch (register_id) {
    case llvm::codeview::RegisterId::MXCSR:
      return lldb_mxcsr_i386;
    case llvm::codeview::RegisterId::BND0:
      return lldb_bnd0_i386;
    case llvm::codeview::RegisterId::BND1:
      return lldb_bnd1_i386;
    case llvm::codeview::RegisterId::BND2:
      return lldb_bnd2_i386;
    default:
      return LLDB_INVALID_REGNUM;
    }
  case llvm::Triple::x86_64:
    if (static_cast<uint16_t>(register_id) <
        sizeof(g_code_view_to_lldb_registers_x86_64) /
            sizeof(g_code_view_to_lldb_registers_x86_64[0]))
      return g_code_view_to_lldb_registers_x86_64[static_cast<uint16_t>(
          register_id)];

    return LLDB_INVALID_REGNUM;
  default:
    return LLDB_INVALID_REGNUM;
  }
}

uint32_t
lldb_private::npdb::GetRegisterSize(llvm::codeview::RegisterId register_id) {
  switch(register_id) {
    case llvm::codeview::RegisterId::AL:
    case llvm::codeview::RegisterId::BL:
    case llvm::codeview::RegisterId::CL:
    case llvm::codeview::RegisterId::DL:
    case llvm::codeview::RegisterId::AH:
    case llvm::codeview::RegisterId::BH:
    case llvm::codeview::RegisterId::CH:
    case llvm::codeview::RegisterId::DH:
    case llvm::codeview::RegisterId::SIL:
    case llvm::codeview::RegisterId::DIL:
    case llvm::codeview::RegisterId::BPL:
    case llvm::codeview::RegisterId::SPL:
    case llvm::codeview::RegisterId::R8B:
    case llvm::codeview::RegisterId::R9B:
    case llvm::codeview::RegisterId::R10B:
    case llvm::codeview::RegisterId::R11B:
    case llvm::codeview::RegisterId::R12B:
    case llvm::codeview::RegisterId::R13B:
    case llvm::codeview::RegisterId::R14B:
    case llvm::codeview::RegisterId::R15B:
      return 1;
    case llvm::codeview::RegisterId::AX:
    case llvm::codeview::RegisterId::BX:
    case llvm::codeview::RegisterId::CX:
    case llvm::codeview::RegisterId::DX:
    case llvm::codeview::RegisterId::SP:
    case llvm::codeview::RegisterId::BP:
    case llvm::codeview::RegisterId::SI:
    case llvm::codeview::RegisterId::DI:
    case llvm::codeview::RegisterId::R8W:
    case llvm::codeview::RegisterId::R9W:
    case llvm::codeview::RegisterId::R10W:
    case llvm::codeview::RegisterId::R11W:
    case llvm::codeview::RegisterId::R12W:
    case llvm::codeview::RegisterId::R13W:
    case llvm::codeview::RegisterId::R14W:
    case llvm::codeview::RegisterId::R15W:
      return 2;
    case llvm::codeview::RegisterId::EAX:
    case llvm::codeview::RegisterId::EBX:
    case llvm::codeview::RegisterId::ECX:
    case llvm::codeview::RegisterId::EDX:
    case llvm::codeview::RegisterId::ESP:
    case llvm::codeview::RegisterId::EBP:
    case llvm::codeview::RegisterId::ESI:
    case llvm::codeview::RegisterId::EDI:
    case llvm::codeview::RegisterId::R8D:
    case llvm::codeview::RegisterId::R9D:
    case llvm::codeview::RegisterId::R10D:
    case llvm::codeview::RegisterId::R11D:
    case llvm::codeview::RegisterId::R12D:
    case llvm::codeview::RegisterId::R13D:
    case llvm::codeview::RegisterId::R14D:
    case llvm::codeview::RegisterId::R15D:
      return 4;
    case llvm::codeview::RegisterId::RAX:
    case llvm::codeview::RegisterId::RBX:
    case llvm::codeview::RegisterId::RCX:
    case llvm::codeview::RegisterId::RDX:
    case llvm::codeview::RegisterId::RSI:
    case llvm::codeview::RegisterId::RDI:
    case llvm::codeview::RegisterId::RBP:
    case llvm::codeview::RegisterId::RSP:
    case llvm::codeview::RegisterId::R8:
    case llvm::codeview::RegisterId::R9:
    case llvm::codeview::RegisterId::R10:
    case llvm::codeview::RegisterId::R11:
    case llvm::codeview::RegisterId::R12:
    case llvm::codeview::RegisterId::R13:
    case llvm::codeview::RegisterId::R14:
    case llvm::codeview::RegisterId::R15:
      return 8;
    case llvm::codeview::RegisterId::XMM0:
    case llvm::codeview::RegisterId::XMM1:
    case llvm::codeview::RegisterId::XMM2:
    case llvm::codeview::RegisterId::XMM3:
    case llvm::codeview::RegisterId::XMM4:
    case llvm::codeview::RegisterId::XMM5:
    case llvm::codeview::RegisterId::XMM6:
    case llvm::codeview::RegisterId::XMM7:
    case llvm::codeview::RegisterId::XMM8:
    case llvm::codeview::RegisterId::XMM9:
    case llvm::codeview::RegisterId::XMM10:
    case llvm::codeview::RegisterId::XMM11:
    case llvm::codeview::RegisterId::XMM12:
    case llvm::codeview::RegisterId::XMM13:
    case llvm::codeview::RegisterId::XMM14:
    case llvm::codeview::RegisterId::XMM15:
      return 16;
    default:
      return 0;
  }
}
