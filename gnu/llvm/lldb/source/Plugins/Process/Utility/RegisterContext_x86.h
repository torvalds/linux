//===-- RegisterContext_x86.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXT_X86_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXT_X86_H

#include <cstddef>
#include <cstdint>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/Support/Compiler.h"

namespace lldb_private {
// i386 ehframe, dwarf regnums

// Register numbers seen in eh_frame (eRegisterKindEHFrame) on i386 systems
// (non-Darwin)
//
enum {
  ehframe_eax_i386 = 0,
  ehframe_ecx_i386,
  ehframe_edx_i386,
  ehframe_ebx_i386,

  // on Darwin esp & ebp are reversed in the eh_frame section for i386 (versus
  // dwarf's reg numbering).
  // To be specific:
  //    i386+darwin eh_frame:        4 is ebp, 5 is esp
  //    i386+everyone else eh_frame: 4 is esp, 5 is ebp
  //    i386 dwarf:                  4 is esp, 5 is ebp
  // lldb will get the darwin-specific eh_frame reg numberings from debugserver,
  // or the ABI, so we
  // only encode the generally correct 4 == esp, 5 == ebp numbers in this
  // generic header.

  ehframe_esp_i386,
  ehframe_ebp_i386,
  ehframe_esi_i386,
  ehframe_edi_i386,
  ehframe_eip_i386,
  ehframe_eflags_i386,
  ehframe_st0_i386 = 12,
  ehframe_st1_i386,
  ehframe_st2_i386,
  ehframe_st3_i386,
  ehframe_st4_i386,
  ehframe_st5_i386,
  ehframe_st6_i386,
  ehframe_st7_i386,
  ehframe_xmm0_i386 = 21,
  ehframe_xmm1_i386,
  ehframe_xmm2_i386,
  ehframe_xmm3_i386,
  ehframe_xmm4_i386,
  ehframe_xmm5_i386,
  ehframe_xmm6_i386,
  ehframe_xmm7_i386,
  ehframe_mm0_i386 = 29,
  ehframe_mm1_i386,
  ehframe_mm2_i386,
  ehframe_mm3_i386,
  ehframe_mm4_i386,
  ehframe_mm5_i386,
  ehframe_mm6_i386,
  ehframe_mm7_i386,
};

// DWARF register numbers (eRegisterKindDWARF)
// Intel's x86 or IA-32
enum {
  // General Purpose Registers.
  dwarf_eax_i386 = 0,
  dwarf_ecx_i386,
  dwarf_edx_i386,
  dwarf_ebx_i386,
  dwarf_esp_i386,
  dwarf_ebp_i386,
  dwarf_esi_i386,
  dwarf_edi_i386,
  dwarf_eip_i386,
  dwarf_eflags_i386,
  // Floating Point Registers
  dwarf_st0_i386 = 11,
  dwarf_st1_i386,
  dwarf_st2_i386,
  dwarf_st3_i386,
  dwarf_st4_i386,
  dwarf_st5_i386,
  dwarf_st6_i386,
  dwarf_st7_i386,
  // SSE Registers
  dwarf_xmm0_i386 = 21,
  dwarf_xmm1_i386,
  dwarf_xmm2_i386,
  dwarf_xmm3_i386,
  dwarf_xmm4_i386,
  dwarf_xmm5_i386,
  dwarf_xmm6_i386,
  dwarf_xmm7_i386,
  // MMX Registers
  dwarf_mm0_i386 = 29,
  dwarf_mm1_i386,
  dwarf_mm2_i386,
  dwarf_mm3_i386,
  dwarf_mm4_i386,
  dwarf_mm5_i386,
  dwarf_mm6_i386,
  dwarf_mm7_i386,
  dwarf_fctrl_i386 = 37, // x87 control word
  dwarf_fstat_i386 = 38, // x87 status word
  dwarf_mxcsr_i386 = 39,
  dwarf_es_i386 = 40,
  dwarf_cs_i386 = 41,
  dwarf_ss_i386 = 42,
  dwarf_ds_i386 = 43,
  dwarf_fs_i386 = 44,
  dwarf_gs_i386 = 45,

  // I believe the ymm registers use the dwarf_xmm%_i386 register numbers and
  //  then differentiate based on size of the register.
  dwarf_bnd0_i386 = 101,
  dwarf_bnd1_i386,
  dwarf_bnd2_i386,
  dwarf_bnd3_i386,
};

// AMD x86_64, AMD64, Intel EM64T, or Intel 64 ehframe, dwarf regnums

// EHFrame and DWARF Register numbers (eRegisterKindEHFrame &
// eRegisterKindDWARF)
//  This is the spec I used (as opposed to x86-64-abi-0.99.pdf):
//  http://software.intel.com/sites/default/files/article/402129/mpx-linux64-abi.pdf
enum {
  // GP Registers
  dwarf_rax_x86_64 = 0,
  dwarf_rdx_x86_64,
  dwarf_rcx_x86_64,
  dwarf_rbx_x86_64,
  dwarf_rsi_x86_64,
  dwarf_rdi_x86_64,
  dwarf_rbp_x86_64,
  dwarf_rsp_x86_64,
  // Extended GP Registers
  dwarf_r8_x86_64 = 8,
  dwarf_r9_x86_64,
  dwarf_r10_x86_64,
  dwarf_r11_x86_64,
  dwarf_r12_x86_64,
  dwarf_r13_x86_64,
  dwarf_r14_x86_64,
  dwarf_r15_x86_64,
  // Return Address (RA) mapped to RIP
  dwarf_rip_x86_64 = 16,
  // SSE Vector Registers
  dwarf_xmm0_x86_64 = 17,
  dwarf_xmm1_x86_64,
  dwarf_xmm2_x86_64,
  dwarf_xmm3_x86_64,
  dwarf_xmm4_x86_64,
  dwarf_xmm5_x86_64,
  dwarf_xmm6_x86_64,
  dwarf_xmm7_x86_64,
  dwarf_xmm8_x86_64,
  dwarf_xmm9_x86_64,
  dwarf_xmm10_x86_64,
  dwarf_xmm11_x86_64,
  dwarf_xmm12_x86_64,
  dwarf_xmm13_x86_64,
  dwarf_xmm14_x86_64,
  dwarf_xmm15_x86_64,
  // Floating Point Registers
  dwarf_st0_x86_64 = 33,
  dwarf_st1_x86_64,
  dwarf_st2_x86_64,
  dwarf_st3_x86_64,
  dwarf_st4_x86_64,
  dwarf_st5_x86_64,
  dwarf_st6_x86_64,
  dwarf_st7_x86_64,
  // MMX Registers
  dwarf_mm0_x86_64 = 41,
  dwarf_mm1_x86_64,
  dwarf_mm2_x86_64,
  dwarf_mm3_x86_64,
  dwarf_mm4_x86_64,
  dwarf_mm5_x86_64,
  dwarf_mm6_x86_64,
  dwarf_mm7_x86_64,
  // Control and Status Flags Register
  dwarf_rflags_x86_64 = 49,
  //  selector registers
  dwarf_es_x86_64 = 50,
  dwarf_cs_x86_64,
  dwarf_ss_x86_64,
  dwarf_ds_x86_64,
  dwarf_fs_x86_64,
  dwarf_gs_x86_64,
  // Base registers
  dwarf_fs_base_x86_64 = 58,
  dwarf_gs_base_x86_64 = 59,
  // Floating point control registers
  dwarf_mxcsr_x86_64 = 64, // Media Control and Status
  dwarf_fctrl_x86_64,      // x87 control word
  dwarf_fstat_x86_64,      // x87 status word
  // Upper Vector Registers
  dwarf_ymm0h_x86_64 = 67,
  dwarf_ymm1h_x86_64,
  dwarf_ymm2h_x86_64,
  dwarf_ymm3h_x86_64,
  dwarf_ymm4h_x86_64,
  dwarf_ymm5h_x86_64,
  dwarf_ymm6h_x86_64,
  dwarf_ymm7h_x86_64,
  dwarf_ymm8h_x86_64,
  dwarf_ymm9h_x86_64,
  dwarf_ymm10h_x86_64,
  dwarf_ymm11h_x86_64,
  dwarf_ymm12h_x86_64,
  dwarf_ymm13h_x86_64,
  dwarf_ymm14h_x86_64,
  dwarf_ymm15h_x86_64,
  // MPX registers
  dwarf_bnd0_x86_64 = 126,
  dwarf_bnd1_x86_64,
  dwarf_bnd2_x86_64,
  dwarf_bnd3_x86_64,
  // AVX2 Vector Mask Registers
  // dwarf_k0_x86_64 = 118,
  // dwarf_k1_x86_64,
  // dwarf_k2_x86_64,
  // dwarf_k3_x86_64,
  // dwarf_k4_x86_64,
  // dwarf_k5_x86_64,
  // dwarf_k6_x86_64,
  // dwarf_k7_x86_64,
};

// Generic floating-point registers

LLVM_PACKED_START
struct MMSRegComp {
  uint64_t mantissa;
  uint16_t sign_exp;
};

struct MMSReg {
  union {
    uint8_t bytes[10];
    MMSRegComp comp;
  };
  uint8_t pad[6];
};
LLVM_PACKED_END

static_assert(sizeof(MMSRegComp) == 10, "MMSRegComp is not 10 bytes of size");
static_assert(sizeof(MMSReg) == 16, "MMSReg is not 16 bytes of size");

struct XMMReg {
  uint8_t bytes[16]; // 128-bits for each XMM register
};

// i387_fxsave_struct
struct FXSAVE {
  uint16_t fctrl;     // FPU Control Word (fcw)
  uint16_t fstat;     // FPU Status Word (fsw)
  uint16_t ftag;      // FPU Tag Word (ftw)
  uint16_t fop;       // Last Instruction Opcode (fop)
  union {
    struct {
      uint64_t fip; // Instruction Pointer
      uint64_t fdp; // Data Pointer
    } x86_64;
    struct {
      uint32_t fioff; // FPU IP Offset (fip)
      uint32_t fiseg; // FPU IP Selector (fcs)
      uint32_t fooff; // FPU Operand Pointer Offset (foo)
      uint32_t foseg; // FPU Operand Pointer Selector (fos)
    } i386_; // Added _ in the end to avoid error with gcc defining i386 in some
             // cases
  } ptr;
  uint32_t mxcsr;     // MXCSR Register State
  uint32_t mxcsrmask; // MXCSR Mask
  MMSReg stmm[8];     // 8*16 bytes for each FP-reg = 128 bytes
  XMMReg xmm[16];     // 16*16 bytes for each XMM-reg = 256 bytes
  uint8_t padding1[48];
  uint64_t xcr0;
  uint8_t padding2[40];
};

// Extended floating-point registers

struct YMMHReg {
  uint8_t bytes[16]; // 16 * 8 bits for the high bytes of each YMM register
};

struct YMMReg {
  uint8_t bytes[32]; // 16 * 16 bits for each YMM register
};

struct YMM {
  YMMReg ymm[16]; // assembled from ymmh and xmm registers
};

struct MPXReg {
  uint8_t bytes[16]; // MPX 128 bit bound registers
};

struct MPXCsr {
  uint8_t bytes[8]; // MPX 64 bit bndcfgu and bndstatus registers (collectively
                    // BNDCSR state)
};

struct MPX {
  MPXReg mpxr[4];
  MPXCsr mpxc[2];
};

LLVM_PACKED_START
struct XSAVE_HDR {
  enum class XFeature : uint64_t {
    FP = 1,
    SSE = FP << 1,
    YMM = SSE << 1,
    BNDREGS = YMM << 1,
    BNDCSR = BNDREGS << 1,
    OPMASK = BNDCSR << 1,
    ZMM_Hi256 = OPMASK << 1,
    Hi16_ZMM = ZMM_Hi256 << 1,
    PT = Hi16_ZMM << 1,
    PKRU = PT << 1,
    LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue*/ PKRU)
  };

  XFeature xstate_bv; // OS enabled xstate mask to determine the extended states
                      // supported by the processor
  XFeature xcomp_bv;  // Mask to indicate the format of the XSAVE area and of
                      // the XRSTOR instruction
  uint64_t reserved1[1];
  uint64_t reserved2[5];
};
static_assert(sizeof(XSAVE_HDR) == 64, "XSAVE_HDR layout incorrect");
LLVM_PACKED_END

// x86 extensions to FXSAVE (i.e. for AVX and MPX processors)
LLVM_PACKED_START
struct XSAVE {
  FXSAVE i387;      // floating point registers typical in i387_fxsave_struct
  XSAVE_HDR header; // The xsave_hdr_struct can be used to determine if the
                    // following extensions are usable
  YMMHReg ymmh[16]; // High 16 bytes of each of 16 YMM registers (the low bytes
                    // are in FXSAVE.xmm for compatibility with SSE)
  uint64_t reserved3[16];
  MPXReg mpxr[4];   // MPX BNDREG state, containing 128-bit bound registers
  MPXCsr mpxc[2];   // MPX BNDCSR state, containing 64-bit BNDCFGU and
                    // BNDSTATUS registers
};
LLVM_PACKED_END

// Floating-point registers
union FPR {
  FXSAVE fxsave; // Generic floating-point registers.
  XSAVE xsave;   // x86 extended processor state.
};

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

// Convenience function to combine YMM register data from XSAVE-style input.
inline YMMReg XStateToYMM(const void* xmm_bytes, const void* ymmh_bytes) {
  YMMReg ret;

  ::memcpy(ret.bytes, xmm_bytes, sizeof(XMMReg));
  ::memcpy(ret.bytes + sizeof(XMMReg), ymmh_bytes, sizeof(YMMHReg));

  return ret;
}

// Convenience function to copy YMM register data into XSAVE-style output.
inline void YMMToXState(const YMMReg& input, void* xmm_bytes, void* ymmh_bytes) {
  ::memcpy(xmm_bytes, input.bytes, sizeof(XMMReg));
  ::memcpy(ymmh_bytes, input.bytes + sizeof(XMMReg), sizeof(YMMHReg));
}

uint16_t AbridgedToFullTagWord(uint8_t abridged_tw, uint16_t sw,
                               llvm::ArrayRef<MMSReg> st_regs);
uint8_t FullToAbridgedTagWord(uint16_t tw);

} // namespace lldb_private

#endif
