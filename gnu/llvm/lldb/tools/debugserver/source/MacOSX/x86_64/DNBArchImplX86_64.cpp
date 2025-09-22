//===-- DNBArchImplX86_64.cpp -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/25/07.
//
//===----------------------------------------------------------------------===//

#if defined(__i386__) || defined(__x86_64__)

#include <sys/cdefs.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include "DNBLog.h"
#include "MacOSX/x86_64/DNBArchImplX86_64.h"
#include "MachProcess.h"
#include "MachThread.h"
#include <cstdlib>
#include <mach/mach.h>

#if defined(LLDB_DEBUGSERVER_RELEASE) || defined(LLDB_DEBUGSERVER_DEBUG)
enum debugState { debugStateUnknown, debugStateOff, debugStateOn };

static debugState sFPUDebugState = debugStateUnknown;
static debugState sAVXForceState = debugStateUnknown;

static bool DebugFPURegs() {
  if (sFPUDebugState == debugStateUnknown) {
    if (getenv("DNB_DEBUG_FPU_REGS"))
      sFPUDebugState = debugStateOn;
    else
      sFPUDebugState = debugStateOff;
  }

  return (sFPUDebugState == debugStateOn);
}

static bool ForceAVXRegs() {
  if (sFPUDebugState == debugStateUnknown) {
    if (getenv("DNB_DEBUG_X86_FORCE_AVX_REGS"))
      sAVXForceState = debugStateOn;
    else
      sAVXForceState = debugStateOff;
  }

  return (sAVXForceState == debugStateOn);
}

#define DEBUG_FPU_REGS (DebugFPURegs())
#define FORCE_AVX_REGS (ForceAVXRegs())
#else
#define DEBUG_FPU_REGS (0)
#define FORCE_AVX_REGS (0)
#endif

bool DetectHardwareFeature(const char *feature) {
  int answer = 0;
  size_t answer_size = sizeof(answer);
  int error = ::sysctlbyname(feature, &answer, &answer_size, NULL, 0);
  return error == 0 && answer != 0;
}

enum AVXPresence { eAVXUnknown = -1, eAVXNotPresent = 0, eAVXPresent = 1 };

bool LogAVXAndReturn(AVXPresence has_avx, int err, const char * os_ver) {
  DNBLogThreadedIf(LOG_THREAD,
                   "CPUHasAVX(): g_has_avx = %i (err = %i, os_ver = %s)",
                   has_avx, err, os_ver);
  return (has_avx == eAVXPresent);
}

extern "C" bool CPUHasAVX() {
  static AVXPresence g_has_avx = eAVXUnknown;
  if (g_has_avx != eAVXUnknown)
    return LogAVXAndReturn(g_has_avx, 0, "");

  g_has_avx = eAVXNotPresent;

  // OS X 10.7.3 and earlier have a bug in thread_get_state that truncated the
  // size of the return. To work around this we have to disable AVX debugging
  // on hosts prior to 10.7.3 (<rdar://problem/10122874>).
  int mib[2];
  char buffer[1024];
  size_t length = sizeof(buffer);
  mib[0] = CTL_KERN;
  mib[1] = KERN_OSVERSION;

  // KERN_OSVERSION returns the build number which is a number signifying the
  // major version, a capitol letter signifying the minor version, and numbers
  // signifying the build (ex: on 10.12.3, the returned value is 16D32).
  int err = ::sysctl(mib, 2, &buffer, &length, NULL, 0);
  if (err != 0)
    return LogAVXAndReturn(g_has_avx, err, "");

  size_t first_letter = 0;
  for (; first_letter < length; ++first_letter) {
    // This is looking for the first uppercase letter
    if (isupper(buffer[first_letter]))
      break;
  }
  char letter = buffer[first_letter];
  buffer[first_letter] = '\0';
  auto major_ver = strtoull(buffer, NULL, 0);
  buffer[first_letter] = letter;

  // In this check we're looking to see that our major and minor version numer
  // was >= 11E, which is the 10.7.4 release.
  if (major_ver < 11 || (major_ver == 11 && letter < 'E'))
    return LogAVXAndReturn(g_has_avx, err, buffer);
  if (DetectHardwareFeature("hw.optional.avx1_0"))
    g_has_avx = eAVXPresent;

  return LogAVXAndReturn(g_has_avx, err, buffer);
}

extern "C" bool CPUHasAVX512f() {
  static AVXPresence g_has_avx512f = eAVXUnknown;
  if (g_has_avx512f != eAVXUnknown)
    return g_has_avx512f == eAVXPresent;

  g_has_avx512f = DetectHardwareFeature("hw.optional.avx512f") ? eAVXPresent
                                                               : eAVXNotPresent;

  return (g_has_avx512f == eAVXPresent);
}

uint64_t DNBArchImplX86_64::GetPC(uint64_t failValue) {
  // Get program counter
  if (GetGPRState(false) == KERN_SUCCESS)
    return m_state.context.gpr.__rip;
  return failValue;
}

kern_return_t DNBArchImplX86_64::SetPC(uint64_t value) {
  // Get program counter
  kern_return_t err = GetGPRState(false);
  if (err == KERN_SUCCESS) {
    m_state.context.gpr.__rip = value;
    err = SetGPRState();
  }
  return err == KERN_SUCCESS;
}

uint64_t DNBArchImplX86_64::GetSP(uint64_t failValue) {
  // Get stack pointer
  if (GetGPRState(false) == KERN_SUCCESS)
    return m_state.context.gpr.__rsp;
  return failValue;
}

// Uncomment the value below to verify the values in the debugger.
//#define DEBUG_GPR_VALUES 1    // DO NOT CHECK IN WITH THIS DEFINE ENABLED

kern_return_t DNBArchImplX86_64::GetGPRState(bool force) {
  if (force || m_state.GetError(e_regSetGPR, Read)) {
#if DEBUG_GPR_VALUES
    m_state.context.gpr.__rax = ('a' << 8) + 'x';
    m_state.context.gpr.__rbx = ('b' << 8) + 'x';
    m_state.context.gpr.__rcx = ('c' << 8) + 'x';
    m_state.context.gpr.__rdx = ('d' << 8) + 'x';
    m_state.context.gpr.__rdi = ('d' << 8) + 'i';
    m_state.context.gpr.__rsi = ('s' << 8) + 'i';
    m_state.context.gpr.__rbp = ('b' << 8) + 'p';
    m_state.context.gpr.__rsp = ('s' << 8) + 'p';
    m_state.context.gpr.__r8 = ('r' << 8) + '8';
    m_state.context.gpr.__r9 = ('r' << 8) + '9';
    m_state.context.gpr.__r10 = ('r' << 8) + 'a';
    m_state.context.gpr.__r11 = ('r' << 8) + 'b';
    m_state.context.gpr.__r12 = ('r' << 8) + 'c';
    m_state.context.gpr.__r13 = ('r' << 8) + 'd';
    m_state.context.gpr.__r14 = ('r' << 8) + 'e';
    m_state.context.gpr.__r15 = ('r' << 8) + 'f';
    m_state.context.gpr.__rip = ('i' << 8) + 'p';
    m_state.context.gpr.__rflags = ('f' << 8) + 'l';
    m_state.context.gpr.__cs = ('c' << 8) + 's';
    m_state.context.gpr.__fs = ('f' << 8) + 's';
    m_state.context.gpr.__gs = ('g' << 8) + 's';
    m_state.SetError(e_regSetGPR, Read, 0);
#else
    mach_msg_type_number_t count = e_regSetWordSizeGPR;
    m_state.SetError(
        e_regSetGPR, Read,
        ::thread_get_state(m_thread->MachPortNumber(), __x86_64_THREAD_STATE,
                           (thread_state_t)&m_state.context.gpr, &count));
    DNBLogThreadedIf(
        LOG_THREAD,
        "::thread_get_state (0x%4.4x, %u, &gpr, %u) => 0x%8.8x"
        "\n\trax = %16.16llx rbx = %16.16llx rcx = %16.16llx rdx = %16.16llx"
        "\n\trdi = %16.16llx rsi = %16.16llx rbp = %16.16llx rsp = %16.16llx"
        "\n\t r8 = %16.16llx  r9 = %16.16llx r10 = %16.16llx r11 = %16.16llx"
        "\n\tr12 = %16.16llx r13 = %16.16llx r14 = %16.16llx r15 = %16.16llx"
        "\n\trip = %16.16llx"
        "\n\tflg = %16.16llx  cs = %16.16llx  fs = %16.16llx  gs = %16.16llx",
        m_thread->MachPortNumber(), x86_THREAD_STATE64,
        x86_THREAD_STATE64_COUNT, m_state.GetError(e_regSetGPR, Read),
        m_state.context.gpr.__rax, m_state.context.gpr.__rbx,
        m_state.context.gpr.__rcx, m_state.context.gpr.__rdx,
        m_state.context.gpr.__rdi, m_state.context.gpr.__rsi,
        m_state.context.gpr.__rbp, m_state.context.gpr.__rsp,
        m_state.context.gpr.__r8, m_state.context.gpr.__r9,
        m_state.context.gpr.__r10, m_state.context.gpr.__r11,
        m_state.context.gpr.__r12, m_state.context.gpr.__r13,
        m_state.context.gpr.__r14, m_state.context.gpr.__r15,
        m_state.context.gpr.__rip, m_state.context.gpr.__rflags,
        m_state.context.gpr.__cs, m_state.context.gpr.__fs,
        m_state.context.gpr.__gs);

//      DNBLogThreadedIf (LOG_THREAD, "thread_get_state(0x%4.4x, %u, &gpr, %u)
//      => 0x%8.8x"
//                        "\n\trax = %16.16llx"
//                        "\n\trbx = %16.16llx"
//                        "\n\trcx = %16.16llx"
//                        "\n\trdx = %16.16llx"
//                        "\n\trdi = %16.16llx"
//                        "\n\trsi = %16.16llx"
//                        "\n\trbp = %16.16llx"
//                        "\n\trsp = %16.16llx"
//                        "\n\t r8 = %16.16llx"
//                        "\n\t r9 = %16.16llx"
//                        "\n\tr10 = %16.16llx"
//                        "\n\tr11 = %16.16llx"
//                        "\n\tr12 = %16.16llx"
//                        "\n\tr13 = %16.16llx"
//                        "\n\tr14 = %16.16llx"
//                        "\n\tr15 = %16.16llx"
//                        "\n\trip = %16.16llx"
//                        "\n\tflg = %16.16llx"
//                        "\n\t cs = %16.16llx"
//                        "\n\t fs = %16.16llx"
//                        "\n\t gs = %16.16llx",
//                        m_thread->MachPortNumber(),
//                        x86_THREAD_STATE64,
//                        x86_THREAD_STATE64_COUNT,
//                        m_state.GetError(e_regSetGPR, Read),
//                        m_state.context.gpr.__rax,
//                        m_state.context.gpr.__rbx,
//                        m_state.context.gpr.__rcx,
//                        m_state.context.gpr.__rdx,
//                        m_state.context.gpr.__rdi,
//                        m_state.context.gpr.__rsi,
//                        m_state.context.gpr.__rbp,
//                        m_state.context.gpr.__rsp,
//                        m_state.context.gpr.__r8,
//                        m_state.context.gpr.__r9,
//                        m_state.context.gpr.__r10,
//                        m_state.context.gpr.__r11,
//                        m_state.context.gpr.__r12,
//                        m_state.context.gpr.__r13,
//                        m_state.context.gpr.__r14,
//                        m_state.context.gpr.__r15,
//                        m_state.context.gpr.__rip,
//                        m_state.context.gpr.__rflags,
//                        m_state.context.gpr.__cs,
//                        m_state.context.gpr.__fs,
//                        m_state.context.gpr.__gs);
#endif
  }
  return m_state.GetError(e_regSetGPR, Read);
}

// Uncomment the value below to verify the values in the debugger.
//#define DEBUG_FPU_REGS 1    // DO NOT CHECK IN WITH THIS DEFINE ENABLED

kern_return_t DNBArchImplX86_64::GetFPUState(bool force) {
  if (force || m_state.GetError(e_regSetFPU, Read)) {
    if (DEBUG_FPU_REGS) {
      m_state.context.fpu.no_avx.__fpu_reserved[0] = -1;
      m_state.context.fpu.no_avx.__fpu_reserved[1] = -1;
      *(uint16_t *)&(m_state.context.fpu.no_avx.__fpu_fcw) = 0x1234;
      *(uint16_t *)&(m_state.context.fpu.no_avx.__fpu_fsw) = 0x5678;
      m_state.context.fpu.no_avx.__fpu_ftw = 1;
      m_state.context.fpu.no_avx.__fpu_rsrv1 = UINT8_MAX;
      m_state.context.fpu.no_avx.__fpu_fop = 2;
      m_state.context.fpu.no_avx.__fpu_ip = 3;
      m_state.context.fpu.no_avx.__fpu_cs = 4;
      m_state.context.fpu.no_avx.__fpu_rsrv2 = 5;
      m_state.context.fpu.no_avx.__fpu_dp = 6;
      m_state.context.fpu.no_avx.__fpu_ds = 7;
      m_state.context.fpu.no_avx.__fpu_rsrv3 = UINT16_MAX;
      m_state.context.fpu.no_avx.__fpu_mxcsr = 8;
      m_state.context.fpu.no_avx.__fpu_mxcsrmask = 9;
      for (int i = 0; i < 16; ++i) {
        if (i < 10) {
          m_state.context.fpu.no_avx.__fpu_stmm0.__mmst_reg[i] = 'a';
          m_state.context.fpu.no_avx.__fpu_stmm1.__mmst_reg[i] = 'b';
          m_state.context.fpu.no_avx.__fpu_stmm2.__mmst_reg[i] = 'c';
          m_state.context.fpu.no_avx.__fpu_stmm3.__mmst_reg[i] = 'd';
          m_state.context.fpu.no_avx.__fpu_stmm4.__mmst_reg[i] = 'e';
          m_state.context.fpu.no_avx.__fpu_stmm5.__mmst_reg[i] = 'f';
          m_state.context.fpu.no_avx.__fpu_stmm6.__mmst_reg[i] = 'g';
          m_state.context.fpu.no_avx.__fpu_stmm7.__mmst_reg[i] = 'h';
        } else {
          m_state.context.fpu.no_avx.__fpu_stmm0.__mmst_reg[i] = INT8_MIN;
          m_state.context.fpu.no_avx.__fpu_stmm1.__mmst_reg[i] = INT8_MIN;
          m_state.context.fpu.no_avx.__fpu_stmm2.__mmst_reg[i] = INT8_MIN;
          m_state.context.fpu.no_avx.__fpu_stmm3.__mmst_reg[i] = INT8_MIN;
          m_state.context.fpu.no_avx.__fpu_stmm4.__mmst_reg[i] = INT8_MIN;
          m_state.context.fpu.no_avx.__fpu_stmm5.__mmst_reg[i] = INT8_MIN;
          m_state.context.fpu.no_avx.__fpu_stmm6.__mmst_reg[i] = INT8_MIN;
          m_state.context.fpu.no_avx.__fpu_stmm7.__mmst_reg[i] = INT8_MIN;
        }

        m_state.context.fpu.no_avx.__fpu_xmm0.__xmm_reg[i] = '0';
        m_state.context.fpu.no_avx.__fpu_xmm1.__xmm_reg[i] = '1';
        m_state.context.fpu.no_avx.__fpu_xmm2.__xmm_reg[i] = '2';
        m_state.context.fpu.no_avx.__fpu_xmm3.__xmm_reg[i] = '3';
        m_state.context.fpu.no_avx.__fpu_xmm4.__xmm_reg[i] = '4';
        m_state.context.fpu.no_avx.__fpu_xmm5.__xmm_reg[i] = '5';
        m_state.context.fpu.no_avx.__fpu_xmm6.__xmm_reg[i] = '6';
        m_state.context.fpu.no_avx.__fpu_xmm7.__xmm_reg[i] = '7';
        m_state.context.fpu.no_avx.__fpu_xmm8.__xmm_reg[i] = '8';
        m_state.context.fpu.no_avx.__fpu_xmm9.__xmm_reg[i] = '9';
        m_state.context.fpu.no_avx.__fpu_xmm10.__xmm_reg[i] = 'A';
        m_state.context.fpu.no_avx.__fpu_xmm11.__xmm_reg[i] = 'B';
        m_state.context.fpu.no_avx.__fpu_xmm12.__xmm_reg[i] = 'C';
        m_state.context.fpu.no_avx.__fpu_xmm13.__xmm_reg[i] = 'D';
        m_state.context.fpu.no_avx.__fpu_xmm14.__xmm_reg[i] = 'E';
        m_state.context.fpu.no_avx.__fpu_xmm15.__xmm_reg[i] = 'F';
      }
      for (int i = 0; i < sizeof(m_state.context.fpu.no_avx.__fpu_rsrv4); ++i)
        m_state.context.fpu.no_avx.__fpu_rsrv4[i] = INT8_MIN;
      m_state.context.fpu.no_avx.__fpu_reserved1 = -1;
      
      if (CPUHasAVX() || FORCE_AVX_REGS) {
        for (int i = 0; i < 16; ++i) {
          m_state.context.fpu.avx.__fpu_ymmh0.__xmm_reg[i] = '0' + i;
          m_state.context.fpu.avx.__fpu_ymmh1.__xmm_reg[i] = '1' + i;
          m_state.context.fpu.avx.__fpu_ymmh2.__xmm_reg[i] = '2' + i;
          m_state.context.fpu.avx.__fpu_ymmh3.__xmm_reg[i] = '3' + i;
          m_state.context.fpu.avx.__fpu_ymmh4.__xmm_reg[i] = '4' + i;
          m_state.context.fpu.avx.__fpu_ymmh5.__xmm_reg[i] = '5' + i;
          m_state.context.fpu.avx.__fpu_ymmh6.__xmm_reg[i] = '6' + i;
          m_state.context.fpu.avx.__fpu_ymmh7.__xmm_reg[i] = '7' + i;
          m_state.context.fpu.avx.__fpu_ymmh8.__xmm_reg[i] = '8' + i;
          m_state.context.fpu.avx.__fpu_ymmh9.__xmm_reg[i] = '9' + i;
          m_state.context.fpu.avx.__fpu_ymmh10.__xmm_reg[i] = 'A' + i;
          m_state.context.fpu.avx.__fpu_ymmh11.__xmm_reg[i] = 'B' + i;
          m_state.context.fpu.avx.__fpu_ymmh12.__xmm_reg[i] = 'C' + i;
          m_state.context.fpu.avx.__fpu_ymmh13.__xmm_reg[i] = 'D' + i;
          m_state.context.fpu.avx.__fpu_ymmh14.__xmm_reg[i] = 'E' + i;
          m_state.context.fpu.avx.__fpu_ymmh15.__xmm_reg[i] = 'F' + i;
        }
        for (int i = 0; i < sizeof(m_state.context.fpu.avx.__avx_reserved1); ++i)
          m_state.context.fpu.avx.__avx_reserved1[i] = INT8_MIN;
      }
      if (CPUHasAVX512f() || FORCE_AVX_REGS) {
        for (int i = 0; i < 8; ++i) {
          m_state.context.fpu.avx512f.__fpu_k0.__opmask_reg[i] = '0';
          m_state.context.fpu.avx512f.__fpu_k1.__opmask_reg[i] = '1';
          m_state.context.fpu.avx512f.__fpu_k2.__opmask_reg[i] = '2';
          m_state.context.fpu.avx512f.__fpu_k3.__opmask_reg[i] = '3';
          m_state.context.fpu.avx512f.__fpu_k4.__opmask_reg[i] = '4';
          m_state.context.fpu.avx512f.__fpu_k5.__opmask_reg[i] = '5';
          m_state.context.fpu.avx512f.__fpu_k6.__opmask_reg[i] = '6';
          m_state.context.fpu.avx512f.__fpu_k7.__opmask_reg[i] = '7';
        }

        for (int i = 0; i < 32; ++i) {
          m_state.context.fpu.avx512f.__fpu_zmmh0.__ymm_reg[i] = '0';
          m_state.context.fpu.avx512f.__fpu_zmmh1.__ymm_reg[i] = '1';
          m_state.context.fpu.avx512f.__fpu_zmmh2.__ymm_reg[i] = '2';
          m_state.context.fpu.avx512f.__fpu_zmmh3.__ymm_reg[i] = '3';
          m_state.context.fpu.avx512f.__fpu_zmmh4.__ymm_reg[i] = '4';
          m_state.context.fpu.avx512f.__fpu_zmmh5.__ymm_reg[i] = '5';
          m_state.context.fpu.avx512f.__fpu_zmmh6.__ymm_reg[i] = '6';
          m_state.context.fpu.avx512f.__fpu_zmmh7.__ymm_reg[i] = '7';
          m_state.context.fpu.avx512f.__fpu_zmmh8.__ymm_reg[i] = '8';
          m_state.context.fpu.avx512f.__fpu_zmmh9.__ymm_reg[i] = '9';
          m_state.context.fpu.avx512f.__fpu_zmmh10.__ymm_reg[i] = 'A';
          m_state.context.fpu.avx512f.__fpu_zmmh11.__ymm_reg[i] = 'B';
          m_state.context.fpu.avx512f.__fpu_zmmh12.__ymm_reg[i] = 'C';
          m_state.context.fpu.avx512f.__fpu_zmmh13.__ymm_reg[i] = 'D';
          m_state.context.fpu.avx512f.__fpu_zmmh14.__ymm_reg[i] = 'E';
          m_state.context.fpu.avx512f.__fpu_zmmh15.__ymm_reg[i] = 'F';
        }
        for (int i = 0; i < 64; ++i) {
          m_state.context.fpu.avx512f.__fpu_zmm16.__zmm_reg[i] = 'G';
          m_state.context.fpu.avx512f.__fpu_zmm17.__zmm_reg[i] = 'H';
          m_state.context.fpu.avx512f.__fpu_zmm18.__zmm_reg[i] = 'I';
          m_state.context.fpu.avx512f.__fpu_zmm19.__zmm_reg[i] = 'J';
          m_state.context.fpu.avx512f.__fpu_zmm20.__zmm_reg[i] = 'K';
          m_state.context.fpu.avx512f.__fpu_zmm21.__zmm_reg[i] = 'L';
          m_state.context.fpu.avx512f.__fpu_zmm22.__zmm_reg[i] = 'M';
          m_state.context.fpu.avx512f.__fpu_zmm23.__zmm_reg[i] = 'N';
          m_state.context.fpu.avx512f.__fpu_zmm24.__zmm_reg[i] = 'O';
          m_state.context.fpu.avx512f.__fpu_zmm25.__zmm_reg[i] = 'P';
          m_state.context.fpu.avx512f.__fpu_zmm26.__zmm_reg[i] = 'Q';
          m_state.context.fpu.avx512f.__fpu_zmm27.__zmm_reg[i] = 'R';
          m_state.context.fpu.avx512f.__fpu_zmm28.__zmm_reg[i] = 'S';
          m_state.context.fpu.avx512f.__fpu_zmm29.__zmm_reg[i] = 'T';
          m_state.context.fpu.avx512f.__fpu_zmm30.__zmm_reg[i] = 'U';
          m_state.context.fpu.avx512f.__fpu_zmm31.__zmm_reg[i] = 'V';
        }
      }
      m_state.SetError(e_regSetFPU, Read, 0);
    } else {
      mach_msg_type_number_t count = e_regSetWordSizeFPU;
      int flavor = __x86_64_FLOAT_STATE;
      // On a machine with the AVX512 register set, a process only gets a
      // full AVX512 register context after it uses the AVX512 registers;
      // if the process has not yet triggered this change, trying to fetch
      // the AVX512 registers will fail.  Fall through to fetching the AVX
      // registers.
      if (CPUHasAVX512f() || FORCE_AVX_REGS) {
        count = e_regSetWordSizeAVX512f;
        flavor = __x86_64_AVX512F_STATE;
        m_state.SetError(e_regSetFPU, Read,
                         ::thread_get_state(m_thread->MachPortNumber(), flavor,
                                            (thread_state_t)&m_state.context.fpu,
                                          &count));
        DNBLogThreadedIf(LOG_THREAD,
                         "::thread_get_state (0x%4.4x, %u, &fpu, %u => 0x%8.8x",
                         m_thread->MachPortNumber(), flavor, (uint32_t)count,
                         m_state.GetError(e_regSetFPU, Read));

        if (m_state.GetError(e_regSetFPU, Read) == KERN_SUCCESS)
          return m_state.GetError(e_regSetFPU, Read);
        else
          DNBLogThreadedIf(LOG_THREAD,
              "::thread_get_state attempted fetch of avx512 fpu regctx failed, will try fetching avx");
      }
      if (CPUHasAVX() || FORCE_AVX_REGS) {
        count = e_regSetWordSizeAVX;
        flavor = __x86_64_AVX_STATE;
      }
      m_state.SetError(e_regSetFPU, Read,
                       ::thread_get_state(m_thread->MachPortNumber(), flavor,
                                          (thread_state_t)&m_state.context.fpu,
                                          &count));
      DNBLogThreadedIf(LOG_THREAD,
                       "::thread_get_state (0x%4.4x, %u, &fpu, %u => 0x%8.8x",
                       m_thread->MachPortNumber(), flavor, (uint32_t)count,
                       m_state.GetError(e_regSetFPU, Read));
    }
  }
  return m_state.GetError(e_regSetFPU, Read);
}

kern_return_t DNBArchImplX86_64::GetEXCState(bool force) {
  if (force || m_state.GetError(e_regSetEXC, Read)) {
    mach_msg_type_number_t count = e_regSetWordSizeEXC;
    m_state.SetError(
        e_regSetEXC, Read,
        ::thread_get_state(m_thread->MachPortNumber(), __x86_64_EXCEPTION_STATE,
                           (thread_state_t)&m_state.context.exc, &count));
  }
  return m_state.GetError(e_regSetEXC, Read);
}

kern_return_t DNBArchImplX86_64::SetGPRState() {
  kern_return_t kret = ::thread_abort_safely(m_thread->MachPortNumber());
  DNBLogThreadedIf(
      LOG_THREAD, "thread = 0x%4.4x calling thread_abort_safely (tid) => %u "
                  "(SetGPRState() for stop_count = %u)",
      m_thread->MachPortNumber(), kret, m_thread->Process()->StopCount());

  m_state.SetError(e_regSetGPR, Write,
                   ::thread_set_state(m_thread->MachPortNumber(),
                                      __x86_64_THREAD_STATE,
                                      (thread_state_t)&m_state.context.gpr,
                                      e_regSetWordSizeGPR));
  DNBLogThreadedIf(
      LOG_THREAD,
      "::thread_set_state (0x%4.4x, %u, &gpr, %u) => 0x%8.8x"
      "\n\trax = %16.16llx rbx = %16.16llx rcx = %16.16llx rdx = %16.16llx"
      "\n\trdi = %16.16llx rsi = %16.16llx rbp = %16.16llx rsp = %16.16llx"
      "\n\t r8 = %16.16llx  r9 = %16.16llx r10 = %16.16llx r11 = %16.16llx"
      "\n\tr12 = %16.16llx r13 = %16.16llx r14 = %16.16llx r15 = %16.16llx"
      "\n\trip = %16.16llx"
      "\n\tflg = %16.16llx  cs = %16.16llx  fs = %16.16llx  gs = %16.16llx",
      m_thread->MachPortNumber(), __x86_64_THREAD_STATE, e_regSetWordSizeGPR,
      m_state.GetError(e_regSetGPR, Write), m_state.context.gpr.__rax,
      m_state.context.gpr.__rbx, m_state.context.gpr.__rcx,
      m_state.context.gpr.__rdx, m_state.context.gpr.__rdi,
      m_state.context.gpr.__rsi, m_state.context.gpr.__rbp,
      m_state.context.gpr.__rsp, m_state.context.gpr.__r8,
      m_state.context.gpr.__r9, m_state.context.gpr.__r10,
      m_state.context.gpr.__r11, m_state.context.gpr.__r12,
      m_state.context.gpr.__r13, m_state.context.gpr.__r14,
      m_state.context.gpr.__r15, m_state.context.gpr.__rip,
      m_state.context.gpr.__rflags, m_state.context.gpr.__cs,
      m_state.context.gpr.__fs, m_state.context.gpr.__gs);
  return m_state.GetError(e_regSetGPR, Write);
}

kern_return_t DNBArchImplX86_64::SetFPUState() {
  if (DEBUG_FPU_REGS) {
    m_state.SetError(e_regSetFPU, Write, 0);
    return m_state.GetError(e_regSetFPU, Write);
  } else {
    int flavor = __x86_64_FLOAT_STATE;
    mach_msg_type_number_t count = e_regSetWordSizeFPU;
    if (CPUHasAVX512f() || FORCE_AVX_REGS) {
      count = e_regSetWordSizeAVX512f;
      flavor = __x86_64_AVX512F_STATE;
      m_state.SetError(
            e_regSetFPU, Write,
            ::thread_set_state(m_thread->MachPortNumber(), flavor,
                               (thread_state_t)&m_state.context.fpu, count));
      if (m_state.GetError(e_regSetFPU, Write) == KERN_SUCCESS)
        return m_state.GetError(e_regSetFPU, Write);
      else
        DNBLogThreadedIf(LOG_THREAD,
            "::thread_get_state attempted save of avx512 fpu regctx failed, will try saving avx regctx");
    } 
    
    if (CPUHasAVX() || FORCE_AVX_REGS) {
      flavor = __x86_64_AVX_STATE;
      count = e_regSetWordSizeAVX;
    }
    m_state.SetError(
          e_regSetFPU, Write,
          ::thread_set_state(m_thread->MachPortNumber(), flavor,
                             (thread_state_t)&m_state.context.fpu, count));
   return m_state.GetError(e_regSetFPU, Write);
  }
}

kern_return_t DNBArchImplX86_64::SetEXCState() {
  m_state.SetError(e_regSetEXC, Write,
                   ::thread_set_state(m_thread->MachPortNumber(),
                                      __x86_64_EXCEPTION_STATE,
                                      (thread_state_t)&m_state.context.exc,
                                      e_regSetWordSizeEXC));
  return m_state.GetError(e_regSetEXC, Write);
}

kern_return_t DNBArchImplX86_64::GetDBGState(bool force) {
  if (force || m_state.GetError(e_regSetDBG, Read)) {
    mach_msg_type_number_t count = e_regSetWordSizeDBG;
    m_state.SetError(
        e_regSetDBG, Read,
        ::thread_get_state(m_thread->MachPortNumber(), __x86_64_DEBUG_STATE,
                           (thread_state_t)&m_state.context.dbg, &count));
  }
  return m_state.GetError(e_regSetDBG, Read);
}

kern_return_t DNBArchImplX86_64::SetDBGState(bool also_set_on_task) {
  m_state.SetError(e_regSetDBG, Write,
                   ::thread_set_state(m_thread->MachPortNumber(),
                                      __x86_64_DEBUG_STATE,
                                      (thread_state_t)&m_state.context.dbg,
                                      e_regSetWordSizeDBG));
  if (also_set_on_task) {
    kern_return_t kret = ::task_set_state(
        m_thread->Process()->Task().TaskPort(), __x86_64_DEBUG_STATE,
        (thread_state_t)&m_state.context.dbg, e_regSetWordSizeDBG);
    if (kret != KERN_SUCCESS)
      DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchImplX86_64::SetDBGState failed "
                                        "to set debug control register state: "
                                        "0x%8.8x.",
                       kret);
  }
  return m_state.GetError(e_regSetDBG, Write);
}

void DNBArchImplX86_64::ThreadWillResume() {
  // Do we need to step this thread? If so, let the mach thread tell us so.
  if (m_thread->IsStepping()) {
    // This is the primary thread, let the arch do anything it needs
    EnableHardwareSingleStep(true);
  }

  // Reset the debug status register, if necessary, before we resume.
  kern_return_t kret = GetDBGState(false);
  DNBLogThreadedIf(
      LOG_WATCHPOINTS,
      "DNBArchImplX86_64::ThreadWillResume() GetDBGState() => 0x%8.8x.", kret);
  if (kret != KERN_SUCCESS)
    return;

  DBG &debug_state = m_state.context.dbg;
  bool need_reset = false;
  uint32_t i, num = NumSupportedHardwareWatchpoints();
  for (i = 0; i < num; ++i)
    if (IsWatchpointHit(debug_state, i))
      need_reset = true;

  if (need_reset) {
    ClearWatchpointHits(debug_state);
    kret = SetDBGState(false);
    DNBLogThreadedIf(
        LOG_WATCHPOINTS,
        "DNBArchImplX86_64::ThreadWillResume() SetDBGState() => 0x%8.8x.",
        kret);
  }
}

bool DNBArchImplX86_64::ThreadDidStop() {
  bool success = true;

  m_state.InvalidateAllRegisterStates();

  // Are we stepping a single instruction?
  if (GetGPRState(true) == KERN_SUCCESS) {
    // We are single stepping, was this the primary thread?
    if (m_thread->IsStepping()) {
      // This was the primary thread, we need to clear the trace
      // bit if so.
      success = EnableHardwareSingleStep(false) == KERN_SUCCESS;
    } else {
      // The MachThread will automatically restore the suspend count
      // in ThreadDidStop(), so we don't need to do anything here if
      // we weren't the primary thread the last time
    }
  }
  return success;
}

bool DNBArchImplX86_64::NotifyException(MachException::Data &exc) {
  switch (exc.exc_type) {
  case EXC_BAD_ACCESS:
    break;
  case EXC_BAD_INSTRUCTION:
    break;
  case EXC_ARITHMETIC:
    break;
  case EXC_EMULATION:
    break;
  case EXC_SOFTWARE:
    break;
  case EXC_BREAKPOINT:
    if (exc.exc_data.size() >= 2 && exc.exc_data[0] == 2) {
      // exc_code = EXC_I386_BPT
      //
      nub_addr_t pc = GetPC(INVALID_NUB_ADDRESS);
      if (pc != INVALID_NUB_ADDRESS && pc > 0) {
        pc -= 1;
        // Check for a breakpoint at one byte prior to the current PC value
        // since the PC will be just past the trap.

        DNBBreakpoint *bp =
            m_thread->Process()->Breakpoints().FindByAddress(pc);
        if (bp) {
          // Backup the PC for i386 since the trap was taken and the PC
          // is at the address following the single byte trap instruction.
          if (m_state.context.gpr.__rip > 0) {
            m_state.context.gpr.__rip = pc;
            // Write the new PC back out
            SetGPRState();
          }
        }
        return true;
      }
    } else if (exc.exc_data.size() >= 2 && exc.exc_data[0] == 1) {
      // exc_code = EXC_I386_SGL
      //
      // Check whether this corresponds to a watchpoint hit event.
      // If yes, set the exc_sub_code to the data break address.
      nub_addr_t addr = 0;
      uint32_t hw_index = GetHardwareWatchpointHit(addr);
      if (hw_index != INVALID_NUB_HW_INDEX) {
        exc.exc_data[1] = addr;
        // Piggyback the hw_index in the exc.data.
        exc.exc_data.push_back(hw_index);
      }

      return true;
    }
    break;
  case EXC_SYSCALL:
    break;
  case EXC_MACH_SYSCALL:
    break;
  case EXC_RPC_ALERT:
    break;
  }
  return false;
}

uint32_t DNBArchImplX86_64::NumSupportedHardwareWatchpoints() {
  // Available debug address registers: dr0, dr1, dr2, dr3.
  return 4;
}

uint32_t DNBArchImplX86_64::NumSupportedHardwareBreakpoints() {
  DNBLogThreadedIf(LOG_BREAKPOINTS,
                   "DNBArchImplX86_64::NumSupportedHardwareBreakpoints");
  return 4;
}

static uint32_t size_and_rw_bits(nub_size_t size, bool read, bool write) {
  uint32_t rw;
  if (read) {
    rw = 0x3; // READ or READ/WRITE
  } else if (write) {
    rw = 0x1; // WRITE
  } else {
    assert(0 && "read and write cannot both be false");
  }

  switch (size) {
  case 1:
    return rw;
  case 2:
    return (0x1 << 2) | rw;
  case 4:
    return (0x3 << 2) | rw;
  case 8:
    return (0x2 << 2) | rw;
  }
  assert(0 && "invalid size, must be one of 1, 2, 4, or 8");
  return 0;
}
void DNBArchImplX86_64::SetWatchpoint(DBG &debug_state, uint32_t hw_index,
                                      nub_addr_t addr, nub_size_t size,
                                      bool read, bool write) {
  // Set both dr7 (debug control register) and dri (debug address register).

  // dr7{7-0} encodes the local/gloabl enable bits:
  //  global enable --. .-- local enable
  //                  | |
  //                  v v
  //      dr0 -> bits{1-0}
  //      dr1 -> bits{3-2}
  //      dr2 -> bits{5-4}
  //      dr3 -> bits{7-6}
  //
  // dr7{31-16} encodes the rw/len bits:
  //  b_x+3, b_x+2, b_x+1, b_x
  //      where bits{x+1, x} => rw
  //            0b00: execute, 0b01: write, 0b11: read-or-write, 0b10: io
  //            read-or-write (unused)
  //      and bits{x+3, x+2} => len
  //            0b00: 1-byte, 0b01: 2-byte, 0b11: 4-byte, 0b10: 8-byte
  //
  //      dr0 -> bits{19-16}
  //      dr1 -> bits{23-20}
  //      dr2 -> bits{27-24}
  //      dr3 -> bits{31-28}
  debug_state.__dr7 |=
      (1 << (2 * hw_index) |
       size_and_rw_bits(size, read, write) << (16 + 4 * hw_index));
  switch (hw_index) {
  case 0:
    debug_state.__dr0 = addr;
    break;
  case 1:
    debug_state.__dr1 = addr;
    break;
  case 2:
    debug_state.__dr2 = addr;
    break;
  case 3:
    debug_state.__dr3 = addr;
    break;
  default:
    assert(0 &&
           "invalid hardware register index, must be one of 0, 1, 2, or 3");
  }
  return;
}

void DNBArchImplX86_64::ClearWatchpoint(DBG &debug_state, uint32_t hw_index) {
  debug_state.__dr7 &= ~(3 << (2 * hw_index));
  switch (hw_index) {
  case 0:
    debug_state.__dr0 = 0;
    break;
  case 1:
    debug_state.__dr1 = 0;
    break;
  case 2:
    debug_state.__dr2 = 0;
    break;
  case 3:
    debug_state.__dr3 = 0;
    break;
  default:
    assert(0 &&
           "invalid hardware register index, must be one of 0, 1, 2, or 3");
  }
  return;
}

bool DNBArchImplX86_64::IsWatchpointVacant(const DBG &debug_state,
                                           uint32_t hw_index) {
  // Check dr7 (debug control register) for local/global enable bits:
  //  global enable --. .-- local enable
  //                  | |
  //                  v v
  //      dr0 -> bits{1-0}
  //      dr1 -> bits{3-2}
  //      dr2 -> bits{5-4}
  //      dr3 -> bits{7-6}
  return (debug_state.__dr7 & (3 << (2 * hw_index))) == 0;
}

// Resets local copy of debug status register to wait for the next debug
// exception.
void DNBArchImplX86_64::ClearWatchpointHits(DBG &debug_state) {
  // See also IsWatchpointHit().
  debug_state.__dr6 = 0;
  return;
}

bool DNBArchImplX86_64::IsWatchpointHit(const DBG &debug_state,
                                        uint32_t hw_index) {
  // Check dr6 (debug status register) whether a watchpoint hits:
  //          is watchpoint hit?
  //                  |
  //                  v
  //      dr0 -> bits{0}
  //      dr1 -> bits{1}
  //      dr2 -> bits{2}
  //      dr3 -> bits{3}
  return (debug_state.__dr6 & (1 << hw_index));
}

nub_addr_t DNBArchImplX86_64::GetWatchAddress(const DBG &debug_state,
                                              uint32_t hw_index) {
  switch (hw_index) {
  case 0:
    return debug_state.__dr0;
  case 1:
    return debug_state.__dr1;
  case 2:
    return debug_state.__dr2;
  case 3:
    return debug_state.__dr3;
  }
  assert(0 && "invalid hardware register index, must be one of 0, 1, 2, or 3");
  return 0;
}

bool DNBArchImplX86_64::StartTransForHWP() {
  if (m_2pc_trans_state != Trans_Done && m_2pc_trans_state != Trans_Rolled_Back)
    DNBLogError("%s inconsistent state detected, expected %d or %d, got: %d",
                __FUNCTION__, Trans_Done, Trans_Rolled_Back, m_2pc_trans_state);
  m_2pc_dbg_checkpoint = m_state.context.dbg;
  m_2pc_trans_state = Trans_Pending;
  return true;
}
bool DNBArchImplX86_64::RollbackTransForHWP() {
  m_state.context.dbg = m_2pc_dbg_checkpoint;
  if (m_2pc_trans_state != Trans_Pending)
    DNBLogError("%s inconsistent state detected, expected %d, got: %d",
                __FUNCTION__, Trans_Pending, m_2pc_trans_state);
  m_2pc_trans_state = Trans_Rolled_Back;
  kern_return_t kret = SetDBGState(false);
  DNBLogThreadedIf(
      LOG_WATCHPOINTS,
      "DNBArchImplX86_64::RollbackTransForHWP() SetDBGState() => 0x%8.8x.",
      kret);

  return kret == KERN_SUCCESS;
}
bool DNBArchImplX86_64::FinishTransForHWP() {
  m_2pc_trans_state = Trans_Done;
  return true;
}
DNBArchImplX86_64::DBG DNBArchImplX86_64::GetDBGCheckpoint() {
  return m_2pc_dbg_checkpoint;
}

void DNBArchImplX86_64::SetHardwareBreakpoint(DBG &debug_state,
                                              uint32_t hw_index,
                                              nub_addr_t addr,
                                              nub_size_t size) {
  // Set both dr7 (debug control register) and dri (debug address register).

  // dr7{7-0} encodes the local/gloabl enable bits:
  //  global enable --. .-- local enable
  //                  | |
  //                  v v
  //      dr0 -> bits{1-0}
  //      dr1 -> bits{3-2}
  //      dr2 -> bits{5-4}
  //      dr3 -> bits{7-6}
  //
  // dr7{31-16} encodes the rw/len bits:
  //  b_x+3, b_x+2, b_x+1, b_x
  //      where bits{x+1, x} => rw
  //            0b00: execute, 0b01: write, 0b11: read-or-write, 0b10: io
  //            read-or-write (unused)
  //      and bits{x+3, x+2} => len
  //            0b00: 1-byte, 0b01: 2-byte, 0b11: 4-byte, 0b10: 8-byte
  //
  //      dr0 -> bits{19-16}
  //      dr1 -> bits{23-20}
  //      dr2 -> bits{27-24}
  //      dr3 -> bits{31-28}
  debug_state.__dr7 |= (1 << (2 * hw_index) | 0 << (16 + 4 * hw_index));

  switch (hw_index) {
  case 0:
    debug_state.__dr0 = addr;
    break;
  case 1:
    debug_state.__dr1 = addr;
    break;
  case 2:
    debug_state.__dr2 = addr;
    break;
  case 3:
    debug_state.__dr3 = addr;
    break;
  default:
    assert(0 &&
           "invalid hardware register index, must be one of 0, 1, 2, or 3");
  }
  return;
}

uint32_t DNBArchImplX86_64::EnableHardwareBreakpoint(nub_addr_t addr,
                                                     nub_size_t size,
                                                     bool also_set_on_task) {
  DNBLogThreadedIf(LOG_BREAKPOINTS,
                   "DNBArchImplX86_64::EnableHardwareBreakpoint( addr = "
                   "0x%8.8llx, size = %llu )",
                   (uint64_t)addr, (uint64_t)size);

  const uint32_t num_hw_breakpoints = NumSupportedHardwareBreakpoints();
  // Read the debug state
  kern_return_t kret = GetDBGState(false);

  if (kret != KERN_SUCCESS) {
    return INVALID_NUB_HW_INDEX;
  }

  // Check to make sure we have the needed hardware support
  uint32_t i = 0;

  DBG &debug_state = m_state.context.dbg;
  for (i = 0; i < num_hw_breakpoints; ++i) {
    if (IsWatchpointVacant(debug_state, i)) {
      break;
    }
  }

  // See if we found an available hw breakpoint slot above
  if (i < num_hw_breakpoints) {
    DNBLogThreadedIf(
        LOG_BREAKPOINTS,
        "DNBArchImplX86_64::EnableHardwareBreakpoint( free slot = %u )", i);

    StartTransForHWP();

    // Modify our local copy of the debug state, first.
    SetHardwareBreakpoint(debug_state, i, addr, size);
    // Now set the watch point in the inferior.
    kret = SetDBGState(also_set_on_task);

    DNBLogThreadedIf(LOG_BREAKPOINTS,
                     "DNBArchImplX86_64::"
                     "EnableHardwareBreakpoint() "
                     "SetDBGState() => 0x%8.8x.",
                     kret);

    if (kret == KERN_SUCCESS) {
      DNBLogThreadedIf(
          LOG_BREAKPOINTS,
          "DNBArchImplX86_64::EnableHardwareBreakpoint( enabled at slot = %u)",
          i);
      return i;
    }
    // Revert to the previous debug state voluntarily.  The transaction
    // coordinator knows that we have failed.
    else {
      m_state.context.dbg = GetDBGCheckpoint();
    }
  } else {
    DNBLogThreadedIf(LOG_BREAKPOINTS,
                     "DNBArchImplX86_64::EnableHardwareBreakpoint(addr = "
                     "0x%8.8llx, size = %llu) => all hardware breakpoint "
                     "resources are being used.",
                     (uint64_t)addr, (uint64_t)size);
  }

  return INVALID_NUB_HW_INDEX;
}

bool DNBArchImplX86_64::DisableHardwareBreakpoint(uint32_t hw_index,
                                                  bool also_set_on_task) {
  kern_return_t kret = GetDBGState(false);

  const uint32_t num_hw_points = NumSupportedHardwareBreakpoints();
  if (kret == KERN_SUCCESS) {
    DBG &debug_state = m_state.context.dbg;
    if (hw_index < num_hw_points &&
        !IsWatchpointVacant(debug_state, hw_index)) {

      StartTransForHWP();

      // Modify our local copy of the debug state, first.
      ClearWatchpoint(debug_state, hw_index);
      // Now disable the watch point in the inferior.
      kret = SetDBGState(true);
      DNBLogThreadedIf(LOG_WATCHPOINTS,
                       "DNBArchImplX86_64::DisableHardwareBreakpoint( %u )",
                       hw_index);

      if (kret == KERN_SUCCESS)
        return true;
      else // Revert to the previous debug state voluntarily.  The transaction
           // coordinator knows that we have failed.
        m_state.context.dbg = GetDBGCheckpoint();
    }
  }
  return false;
}

uint32_t DNBArchImplX86_64::EnableHardwareWatchpoint(nub_addr_t addr,
                                                     nub_size_t size, bool read,
                                                     bool write,
                                                     bool also_set_on_task) {
  DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchImplX86_64::"
                                    "EnableHardwareWatchpoint(addr = 0x%llx, "
                                    "size = %llu, read = %u, write = %u)",
                   (uint64_t)addr, (uint64_t)size, read, write);

  const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();

  // Can only watch 1, 2, 4, or 8 bytes.
  if (!(size == 1 || size == 2 || size == 4 || size == 8))
    return INVALID_NUB_HW_INDEX;

  // We must watch for either read or write
  if (!read && !write)
    return INVALID_NUB_HW_INDEX;

  // Read the debug state
  kern_return_t kret = GetDBGState(false);

  if (kret == KERN_SUCCESS) {
    // Check to make sure we have the needed hardware support
    uint32_t i = 0;

    DBG &debug_state = m_state.context.dbg;
    for (i = 0; i < num_hw_watchpoints; ++i) {
      if (IsWatchpointVacant(debug_state, i))
        break;
    }

    // See if we found an available hw breakpoint slot above
    if (i < num_hw_watchpoints) {
      StartTransForHWP();

      // Modify our local copy of the debug state, first.
      SetWatchpoint(debug_state, i, addr, size, read, write);
      // Now set the watch point in the inferior.
      kret = SetDBGState(also_set_on_task);
      DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchImplX86_64::"
                                        "EnableHardwareWatchpoint() "
                                        "SetDBGState() => 0x%8.8x.",
                       kret);

      if (kret == KERN_SUCCESS)
        return i;
      else // Revert to the previous debug state voluntarily.  The transaction
           // coordinator knows that we have failed.
        m_state.context.dbg = GetDBGCheckpoint();
    } else {
      DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchImplX86_64::"
                                        "EnableHardwareWatchpoint(): All "
                                        "hardware resources (%u) are in use.",
                       num_hw_watchpoints);
    }
  }
  return INVALID_NUB_HW_INDEX;
}

bool DNBArchImplX86_64::DisableHardwareWatchpoint(uint32_t hw_index,
                                                  bool also_set_on_task) {
  kern_return_t kret = GetDBGState(false);

  const uint32_t num_hw_points = NumSupportedHardwareWatchpoints();
  if (kret == KERN_SUCCESS) {
    DBG &debug_state = m_state.context.dbg;
    if (hw_index < num_hw_points &&
        !IsWatchpointVacant(debug_state, hw_index)) {
      StartTransForHWP();

      // Modify our local copy of the debug state, first.
      ClearWatchpoint(debug_state, hw_index);
      // Now disable the watch point in the inferior.
      kret = SetDBGState(also_set_on_task);
      DNBLogThreadedIf(LOG_WATCHPOINTS,
                       "DNBArchImplX86_64::DisableHardwareWatchpoint( %u )",
                       hw_index);

      if (kret == KERN_SUCCESS)
        return true;
      else // Revert to the previous debug state voluntarily.  The transaction
           // coordinator knows that we have failed.
        m_state.context.dbg = GetDBGCheckpoint();
    }
  }
  return false;
}

// Iterate through the debug status register; return the index of the first hit.
uint32_t DNBArchImplX86_64::GetHardwareWatchpointHit(nub_addr_t &addr) {
  // Read the debug state
  kern_return_t kret = GetDBGState(true);
  DNBLogThreadedIf(
      LOG_WATCHPOINTS,
      "DNBArchImplX86_64::GetHardwareWatchpointHit() GetDBGState() => 0x%8.8x.",
      kret);
  if (kret == KERN_SUCCESS) {
    DBG &debug_state = m_state.context.dbg;
    uint32_t i, num = NumSupportedHardwareWatchpoints();
    for (i = 0; i < num; ++i) {
      if (IsWatchpointHit(debug_state, i)) {
        addr = GetWatchAddress(debug_state, i);
        DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchImplX86_64::"
                                          "GetHardwareWatchpointHit() found => "
                                          "%u (addr = 0x%llx).",
                         i, (uint64_t)addr);
        return i;
      }
    }
  }
  return INVALID_NUB_HW_INDEX;
}

// Set the single step bit in the processor status register.
kern_return_t DNBArchImplX86_64::EnableHardwareSingleStep(bool enable) {
  if (GetGPRState(false) == KERN_SUCCESS) {
    const uint32_t trace_bit = 0x100u;
    if (enable)
      m_state.context.gpr.__rflags |= trace_bit;
    else
      m_state.context.gpr.__rflags &= ~trace_bit;
    return SetGPRState();
  }
  return m_state.GetError(e_regSetGPR, Read);
}

// Register information definitions

enum {
  gpr_rax = 0,
  gpr_rbx,
  gpr_rcx,
  gpr_rdx,
  gpr_rdi,
  gpr_rsi,
  gpr_rbp,
  gpr_rsp,
  gpr_r8,
  gpr_r9,
  gpr_r10,
  gpr_r11,
  gpr_r12,
  gpr_r13,
  gpr_r14,
  gpr_r15,
  gpr_rip,
  gpr_rflags,
  gpr_cs,
  gpr_fs,
  gpr_gs,
  gpr_eax,
  gpr_ebx,
  gpr_ecx,
  gpr_edx,
  gpr_edi,
  gpr_esi,
  gpr_ebp,
  gpr_esp,
  gpr_r8d,  // Low 32 bits or r8
  gpr_r9d,  // Low 32 bits or r9
  gpr_r10d, // Low 32 bits or r10
  gpr_r11d, // Low 32 bits or r11
  gpr_r12d, // Low 32 bits or r12
  gpr_r13d, // Low 32 bits or r13
  gpr_r14d, // Low 32 bits or r14
  gpr_r15d, // Low 32 bits or r15
  gpr_ax,
  gpr_bx,
  gpr_cx,
  gpr_dx,
  gpr_di,
  gpr_si,
  gpr_bp,
  gpr_sp,
  gpr_r8w,  // Low 16 bits or r8
  gpr_r9w,  // Low 16 bits or r9
  gpr_r10w, // Low 16 bits or r10
  gpr_r11w, // Low 16 bits or r11
  gpr_r12w, // Low 16 bits or r12
  gpr_r13w, // Low 16 bits or r13
  gpr_r14w, // Low 16 bits or r14
  gpr_r15w, // Low 16 bits or r15
  gpr_ah,
  gpr_bh,
  gpr_ch,
  gpr_dh,
  gpr_al,
  gpr_bl,
  gpr_cl,
  gpr_dl,
  gpr_dil,
  gpr_sil,
  gpr_bpl,
  gpr_spl,
  gpr_r8l,  // Low 8 bits or r8
  gpr_r9l,  // Low 8 bits or r9
  gpr_r10l, // Low 8 bits or r10
  gpr_r11l, // Low 8 bits or r11
  gpr_r12l, // Low 8 bits or r12
  gpr_r13l, // Low 8 bits or r13
  gpr_r14l, // Low 8 bits or r14
  gpr_r15l, // Low 8 bits or r15
  k_num_gpr_regs
};

enum {
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
  fpu_xmm8,
  fpu_xmm9,
  fpu_xmm10,
  fpu_xmm11,
  fpu_xmm12,
  fpu_xmm13,
  fpu_xmm14,
  fpu_xmm15,
  fpu_ymm0,
  fpu_ymm1,
  fpu_ymm2,
  fpu_ymm3,
  fpu_ymm4,
  fpu_ymm5,
  fpu_ymm6,
  fpu_ymm7,
  fpu_ymm8,
  fpu_ymm9,
  fpu_ymm10,
  fpu_ymm11,
  fpu_ymm12,
  fpu_ymm13,
  fpu_ymm14,
  fpu_ymm15,
  fpu_k0,
  fpu_k1,
  fpu_k2,
  fpu_k3,
  fpu_k4,
  fpu_k5,
  fpu_k6,
  fpu_k7,
  fpu_zmm0,
  fpu_zmm1,
  fpu_zmm2,
  fpu_zmm3,
  fpu_zmm4,
  fpu_zmm5,
  fpu_zmm6,
  fpu_zmm7,
  fpu_zmm8,
  fpu_zmm9,
  fpu_zmm10,
  fpu_zmm11,
  fpu_zmm12,
  fpu_zmm13,
  fpu_zmm14,
  fpu_zmm15,
  fpu_zmm16,
  fpu_zmm17,
  fpu_zmm18,
  fpu_zmm19,
  fpu_zmm20,
  fpu_zmm21,
  fpu_zmm22,
  fpu_zmm23,
  fpu_zmm24,
  fpu_zmm25,
  fpu_zmm26,
  fpu_zmm27,
  fpu_zmm28,
  fpu_zmm29,
  fpu_zmm30,
  fpu_zmm31,
  k_num_fpu_regs,

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
  exc_trapno,
  exc_err,
  exc_faultvaddr,
  k_num_exc_regs,
};

enum ehframe_dwarf_regnums {
  ehframe_dwarf_rax = 0,
  ehframe_dwarf_rdx = 1,
  ehframe_dwarf_rcx = 2,
  ehframe_dwarf_rbx = 3,
  ehframe_dwarf_rsi = 4,
  ehframe_dwarf_rdi = 5,
  ehframe_dwarf_rbp = 6,
  ehframe_dwarf_rsp = 7,
  ehframe_dwarf_r8,
  ehframe_dwarf_r9,
  ehframe_dwarf_r10,
  ehframe_dwarf_r11,
  ehframe_dwarf_r12,
  ehframe_dwarf_r13,
  ehframe_dwarf_r14,
  ehframe_dwarf_r15,
  ehframe_dwarf_rip,
  ehframe_dwarf_xmm0,
  ehframe_dwarf_xmm1,
  ehframe_dwarf_xmm2,
  ehframe_dwarf_xmm3,
  ehframe_dwarf_xmm4,
  ehframe_dwarf_xmm5,
  ehframe_dwarf_xmm6,
  ehframe_dwarf_xmm7,
  ehframe_dwarf_xmm8,
  ehframe_dwarf_xmm9,
  ehframe_dwarf_xmm10,
  ehframe_dwarf_xmm11,
  ehframe_dwarf_xmm12,
  ehframe_dwarf_xmm13,
  ehframe_dwarf_xmm14,
  ehframe_dwarf_xmm15,
  ehframe_dwarf_stmm0,
  ehframe_dwarf_stmm1,
  ehframe_dwarf_stmm2,
  ehframe_dwarf_stmm3,
  ehframe_dwarf_stmm4,
  ehframe_dwarf_stmm5,
  ehframe_dwarf_stmm6,
  ehframe_dwarf_stmm7,
  ehframe_dwarf_ymm0 = ehframe_dwarf_xmm0,
  ehframe_dwarf_ymm1 = ehframe_dwarf_xmm1,
  ehframe_dwarf_ymm2 = ehframe_dwarf_xmm2,
  ehframe_dwarf_ymm3 = ehframe_dwarf_xmm3,
  ehframe_dwarf_ymm4 = ehframe_dwarf_xmm4,
  ehframe_dwarf_ymm5 = ehframe_dwarf_xmm5,
  ehframe_dwarf_ymm6 = ehframe_dwarf_xmm6,
  ehframe_dwarf_ymm7 = ehframe_dwarf_xmm7,
  ehframe_dwarf_ymm8 = ehframe_dwarf_xmm8,
  ehframe_dwarf_ymm9 = ehframe_dwarf_xmm9,
  ehframe_dwarf_ymm10 = ehframe_dwarf_xmm10,
  ehframe_dwarf_ymm11 = ehframe_dwarf_xmm11,
  ehframe_dwarf_ymm12 = ehframe_dwarf_xmm12,
  ehframe_dwarf_ymm13 = ehframe_dwarf_xmm13,
  ehframe_dwarf_ymm14 = ehframe_dwarf_xmm14,
  ehframe_dwarf_ymm15 = ehframe_dwarf_xmm15,
  ehframe_dwarf_zmm0 = ehframe_dwarf_xmm0,
  ehframe_dwarf_zmm1 = ehframe_dwarf_xmm1,
  ehframe_dwarf_zmm2 = ehframe_dwarf_xmm2,
  ehframe_dwarf_zmm3 = ehframe_dwarf_xmm3,
  ehframe_dwarf_zmm4 = ehframe_dwarf_xmm4,
  ehframe_dwarf_zmm5 = ehframe_dwarf_xmm5,
  ehframe_dwarf_zmm6 = ehframe_dwarf_xmm6,
  ehframe_dwarf_zmm7 = ehframe_dwarf_xmm7,
  ehframe_dwarf_zmm8 = ehframe_dwarf_xmm8,
  ehframe_dwarf_zmm9 = ehframe_dwarf_xmm9,
  ehframe_dwarf_zmm10 = ehframe_dwarf_xmm10,
  ehframe_dwarf_zmm11 = ehframe_dwarf_xmm11,
  ehframe_dwarf_zmm12 = ehframe_dwarf_xmm12,
  ehframe_dwarf_zmm13 = ehframe_dwarf_xmm13,
  ehframe_dwarf_zmm14 = ehframe_dwarf_xmm14,
  ehframe_dwarf_zmm15 = ehframe_dwarf_xmm15,
  ehframe_dwarf_zmm16 = 67,
  ehframe_dwarf_zmm17,
  ehframe_dwarf_zmm18,
  ehframe_dwarf_zmm19,
  ehframe_dwarf_zmm20,
  ehframe_dwarf_zmm21,
  ehframe_dwarf_zmm22,
  ehframe_dwarf_zmm23,
  ehframe_dwarf_zmm24,
  ehframe_dwarf_zmm25,
  ehframe_dwarf_zmm26,
  ehframe_dwarf_zmm27,
  ehframe_dwarf_zmm28,
  ehframe_dwarf_zmm29,
  ehframe_dwarf_zmm30,
  ehframe_dwarf_zmm31,
  ehframe_dwarf_k0 = 118,
  ehframe_dwarf_k1,
  ehframe_dwarf_k2,
  ehframe_dwarf_k3,
  ehframe_dwarf_k4,
  ehframe_dwarf_k5,
  ehframe_dwarf_k6,
  ehframe_dwarf_k7,
};

enum debugserver_regnums {
  debugserver_rax = 0,
  debugserver_rbx = 1,
  debugserver_rcx = 2,
  debugserver_rdx = 3,
  debugserver_rsi = 4,
  debugserver_rdi = 5,
  debugserver_rbp = 6,
  debugserver_rsp = 7,
  debugserver_r8 = 8,
  debugserver_r9 = 9,
  debugserver_r10 = 10,
  debugserver_r11 = 11,
  debugserver_r12 = 12,
  debugserver_r13 = 13,
  debugserver_r14 = 14,
  debugserver_r15 = 15,
  debugserver_rip = 16,
  debugserver_rflags = 17,
  debugserver_cs = 18,
  debugserver_ss = 19,
  debugserver_ds = 20,
  debugserver_es = 21,
  debugserver_fs = 22,
  debugserver_gs = 23,
  debugserver_stmm0 = 24,
  debugserver_stmm1 = 25,
  debugserver_stmm2 = 26,
  debugserver_stmm3 = 27,
  debugserver_stmm4 = 28,
  debugserver_stmm5 = 29,
  debugserver_stmm6 = 30,
  debugserver_stmm7 = 31,
  debugserver_fctrl = 32,
  debugserver_fcw = debugserver_fctrl,
  debugserver_fstat = 33,
  debugserver_fsw = debugserver_fstat,
  debugserver_ftag = 34,
  debugserver_ftw = debugserver_ftag,
  debugserver_fiseg = 35,
  debugserver_fpu_cs = debugserver_fiseg,
  debugserver_fioff = 36,
  debugserver_ip = debugserver_fioff,
  debugserver_foseg = 37,
  debugserver_fpu_ds = debugserver_foseg,
  debugserver_fooff = 38,
  debugserver_dp = debugserver_fooff,
  debugserver_fop = 39,
  debugserver_xmm0 = 40,
  debugserver_xmm1 = 41,
  debugserver_xmm2 = 42,
  debugserver_xmm3 = 43,
  debugserver_xmm4 = 44,
  debugserver_xmm5 = 45,
  debugserver_xmm6 = 46,
  debugserver_xmm7 = 47,
  debugserver_xmm8 = 48,
  debugserver_xmm9 = 49,
  debugserver_xmm10 = 50,
  debugserver_xmm11 = 51,
  debugserver_xmm12 = 52,
  debugserver_xmm13 = 53,
  debugserver_xmm14 = 54,
  debugserver_xmm15 = 55,
  debugserver_mxcsr = 56,
  debugserver_ymm0 = debugserver_xmm0,
  debugserver_ymm1 = debugserver_xmm1,
  debugserver_ymm2 = debugserver_xmm2,
  debugserver_ymm3 = debugserver_xmm3,
  debugserver_ymm4 = debugserver_xmm4,
  debugserver_ymm5 = debugserver_xmm5,
  debugserver_ymm6 = debugserver_xmm6,
  debugserver_ymm7 = debugserver_xmm7,
  debugserver_ymm8 = debugserver_xmm8,
  debugserver_ymm9 = debugserver_xmm9,
  debugserver_ymm10 = debugserver_xmm10,
  debugserver_ymm11 = debugserver_xmm11,
  debugserver_ymm12 = debugserver_xmm12,
  debugserver_ymm13 = debugserver_xmm13,
  debugserver_ymm14 = debugserver_xmm14,
  debugserver_ymm15 = debugserver_xmm15,
  debugserver_zmm0 = debugserver_xmm0,
  debugserver_zmm1 = debugserver_xmm1,
  debugserver_zmm2 = debugserver_xmm2,
  debugserver_zmm3 = debugserver_xmm3,
  debugserver_zmm4 = debugserver_xmm4,
  debugserver_zmm5 = debugserver_xmm5,
  debugserver_zmm6 = debugserver_xmm6,
  debugserver_zmm7 = debugserver_xmm7,
  debugserver_zmm8 = debugserver_xmm8,
  debugserver_zmm9 = debugserver_xmm9,
  debugserver_zmm10 = debugserver_xmm10,
  debugserver_zmm11 = debugserver_xmm11,
  debugserver_zmm12 = debugserver_xmm12,
  debugserver_zmm13 = debugserver_xmm13,
  debugserver_zmm14 = debugserver_xmm14,
  debugserver_zmm15 = debugserver_xmm15,
  debugserver_zmm16 = 67,
  debugserver_zmm17 = 68,
  debugserver_zmm18 = 69,
  debugserver_zmm19 = 70,
  debugserver_zmm20 = 71,
  debugserver_zmm21 = 72,
  debugserver_zmm22 = 73,
  debugserver_zmm23 = 74,
  debugserver_zmm24 = 75,
  debugserver_zmm25 = 76,
  debugserver_zmm26 = 77,
  debugserver_zmm27 = 78,
  debugserver_zmm28 = 79,
  debugserver_zmm29 = 80,
  debugserver_zmm30 = 81,
  debugserver_zmm31 = 82,
  debugserver_k0 = 118,
  debugserver_k1 = 119,
  debugserver_k2 = 120,
  debugserver_k3 = 121,
  debugserver_k4 = 122,
  debugserver_k5 = 123,
  debugserver_k6 = 124,
  debugserver_k7 = 125,
};

#define GPR_OFFSET(reg) (offsetof(DNBArchImplX86_64::GPR, __##reg))
#define FPU_OFFSET(reg)                                                        \
  (offsetof(DNBArchImplX86_64::FPU, __fpu_##reg) +                             \
   offsetof(DNBArchImplX86_64::Context, fpu.no_avx))
#define AVX_OFFSET(reg)                                                        \
  (offsetof(DNBArchImplX86_64::AVX, __fpu_##reg) +                             \
   offsetof(DNBArchImplX86_64::Context, fpu.avx))
#define AVX512F_OFFSET(reg)                                                    \
  (offsetof(DNBArchImplX86_64::AVX512F, __fpu_##reg) +                         \
   offsetof(DNBArchImplX86_64::Context, fpu.avx512f))
#define EXC_OFFSET(reg)                                                        \
  (offsetof(DNBArchImplX86_64::EXC, __##reg) +                                 \
   offsetof(DNBArchImplX86_64::Context, exc))
#define AVX_OFFSET_YMM(n) (AVX_OFFSET(ymmh0) + (32 * n))
#define AVX512F_OFFSET_ZMM(n) (AVX512F_OFFSET(zmmh0) + (64 * n))

#define GPR_SIZE(reg) (sizeof(((DNBArchImplX86_64::GPR *)NULL)->__##reg))
#define FPU_SIZE_UINT(reg)                                                     \
  (sizeof(((DNBArchImplX86_64::FPU *)NULL)->__fpu_##reg))
#define FPU_SIZE_MMST(reg)                                                     \
  (sizeof(((DNBArchImplX86_64::FPU *)NULL)->__fpu_##reg.__mmst_reg))
#define FPU_SIZE_XMM(reg)                                                      \
  (sizeof(((DNBArchImplX86_64::FPU *)NULL)->__fpu_##reg.__xmm_reg))
#define FPU_SIZE_YMM(reg) (32)
#define FPU_SIZE_ZMM(reg) (64)
#define EXC_SIZE(reg) (sizeof(((DNBArchImplX86_64::EXC *)NULL)->__##reg))

// These macros will auto define the register name, alt name, register size,
// register offset, encoding, format and native register. This ensures that
// the register state structures are defined correctly and have the correct
// sizes and offsets.
#define DEFINE_GPR(reg)                                                        \
  {                                                                            \
    e_regSetGPR, gpr_##reg, #reg, NULL, Uint, Hex, GPR_SIZE(reg),              \
        GPR_OFFSET(reg), ehframe_dwarf_##reg, ehframe_dwarf_##reg,             \
        INVALID_NUB_REGNUM, debugserver_##reg, NULL, g_invalidate_##reg        \
  }
#define DEFINE_GPR_ALT(reg, alt, gen)                                          \
  {                                                                            \
    e_regSetGPR, gpr_##reg, #reg, alt, Uint, Hex, GPR_SIZE(reg),               \
        GPR_OFFSET(reg), ehframe_dwarf_##reg, ehframe_dwarf_##reg, gen,        \
        debugserver_##reg, NULL, g_invalidate_##reg                            \
  }
#define DEFINE_GPR_ALT2(reg, alt)                                              \
  {                                                                            \
    e_regSetGPR, gpr_##reg, #reg, alt, Uint, Hex, GPR_SIZE(reg),               \
        GPR_OFFSET(reg), INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,               \
        INVALID_NUB_REGNUM, debugserver_##reg, NULL, NULL                      \
  }
#define DEFINE_GPR_ALT3(reg, alt, gen)                                         \
  {                                                                            \
    e_regSetGPR, gpr_##reg, #reg, alt, Uint, Hex, GPR_SIZE(reg),               \
        GPR_OFFSET(reg), INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, gen,          \
        debugserver_##reg, NULL, NULL                                          \
  }
#define DEFINE_GPR_ALT4(reg, alt, gen)                                         \
  {                                                                            \
    e_regSetGPR, gpr_##reg, #reg, alt, Uint, Hex, GPR_SIZE(reg),               \
        GPR_OFFSET(reg), ehframe_dwarf_##reg, ehframe_dwarf_##reg, gen,        \
        debugserver_##reg, NULL, NULL                                          \
  }

#define DEFINE_GPR_PSEUDO_32(reg32, reg64)                                     \
  {                                                                            \
    e_regSetGPR, gpr_##reg32, #reg32, NULL, Uint, Hex, 4, 0,                   \
        INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,            \
        INVALID_NUB_REGNUM, g_contained_##reg64, g_invalidate_##reg64          \
  }
#define DEFINE_GPR_PSEUDO_16(reg16, reg64)                                     \
  {                                                                            \
    e_regSetGPR, gpr_##reg16, #reg16, NULL, Uint, Hex, 2, 0,                   \
        INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,            \
        INVALID_NUB_REGNUM, g_contained_##reg64, g_invalidate_##reg64          \
  }
#define DEFINE_GPR_PSEUDO_8H(reg8, reg64)                                      \
  {                                                                            \
    e_regSetGPR, gpr_##reg8, #reg8, NULL, Uint, Hex, 1, 1, INVALID_NUB_REGNUM, \
        INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,            \
        g_contained_##reg64, g_invalidate_##reg64                              \
  }
#define DEFINE_GPR_PSEUDO_8L(reg8, reg64)                                      \
  {                                                                            \
    e_regSetGPR, gpr_##reg8, #reg8, NULL, Uint, Hex, 1, 0, INVALID_NUB_REGNUM, \
        INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,            \
        g_contained_##reg64, g_invalidate_##reg64                              \
  }

// General purpose registers for 64 bit

const char *g_contained_rax[] = {"rax", NULL};
const char *g_contained_rbx[] = {"rbx", NULL};
const char *g_contained_rcx[] = {"rcx", NULL};
const char *g_contained_rdx[] = {"rdx", NULL};
const char *g_contained_rdi[] = {"rdi", NULL};
const char *g_contained_rsi[] = {"rsi", NULL};
const char *g_contained_rbp[] = {"rbp", NULL};
const char *g_contained_rsp[] = {"rsp", NULL};
const char *g_contained_r8[] = {"r8", NULL};
const char *g_contained_r9[] = {"r9", NULL};
const char *g_contained_r10[] = {"r10", NULL};
const char *g_contained_r11[] = {"r11", NULL};
const char *g_contained_r12[] = {"r12", NULL};
const char *g_contained_r13[] = {"r13", NULL};
const char *g_contained_r14[] = {"r14", NULL};
const char *g_contained_r15[] = {"r15", NULL};

const char *g_invalidate_rax[] = {"rax", "eax", "ax", "ah", "al", NULL};
const char *g_invalidate_rbx[] = {"rbx", "ebx", "bx", "bh", "bl", NULL};
const char *g_invalidate_rcx[] = {"rcx", "ecx", "cx", "ch", "cl", NULL};
const char *g_invalidate_rdx[] = {"rdx", "edx", "dx", "dh", "dl", NULL};
const char *g_invalidate_rdi[] = {"rdi", "edi", "di", "dil", NULL};
const char *g_invalidate_rsi[] = {"rsi", "esi", "si", "sil", NULL};
const char *g_invalidate_rbp[] = {"rbp", "ebp", "bp", "bpl", NULL};
const char *g_invalidate_rsp[] = {"rsp", "esp", "sp", "spl", NULL};
const char *g_invalidate_r8[] = {"r8", "r8d", "r8w", "r8l", NULL};
const char *g_invalidate_r9[] = {"r9", "r9d", "r9w", "r9l", NULL};
const char *g_invalidate_r10[] = {"r10", "r10d", "r10w", "r10l", NULL};
const char *g_invalidate_r11[] = {"r11", "r11d", "r11w", "r11l", NULL};
const char *g_invalidate_r12[] = {"r12", "r12d", "r12w", "r12l", NULL};
const char *g_invalidate_r13[] = {"r13", "r13d", "r13w", "r13l", NULL};
const char *g_invalidate_r14[] = {"r14", "r14d", "r14w", "r14l", NULL};
const char *g_invalidate_r15[] = {"r15", "r15d", "r15w", "r15l", NULL};

const DNBRegisterInfo DNBArchImplX86_64::g_gpr_registers[] = {
    DEFINE_GPR(rax),
    DEFINE_GPR(rbx),
    DEFINE_GPR_ALT(rcx, "arg4", GENERIC_REGNUM_ARG4),
    DEFINE_GPR_ALT(rdx, "arg3", GENERIC_REGNUM_ARG3),
    DEFINE_GPR_ALT(rdi, "arg1", GENERIC_REGNUM_ARG1),
    DEFINE_GPR_ALT(rsi, "arg2", GENERIC_REGNUM_ARG2),
    DEFINE_GPR_ALT(rbp, "fp", GENERIC_REGNUM_FP),
    DEFINE_GPR_ALT(rsp, "sp", GENERIC_REGNUM_SP),
    DEFINE_GPR_ALT(r8, "arg5", GENERIC_REGNUM_ARG5),
    DEFINE_GPR_ALT(r9, "arg6", GENERIC_REGNUM_ARG6),
    DEFINE_GPR(r10),
    DEFINE_GPR(r11),
    DEFINE_GPR(r12),
    DEFINE_GPR(r13),
    DEFINE_GPR(r14),
    DEFINE_GPR(r15),
    DEFINE_GPR_ALT4(rip, "pc", GENERIC_REGNUM_PC),
    DEFINE_GPR_ALT3(rflags, "flags", GENERIC_REGNUM_FLAGS),
    DEFINE_GPR_ALT2(cs, NULL),
    DEFINE_GPR_ALT2(fs, NULL),
    DEFINE_GPR_ALT2(gs, NULL),
    DEFINE_GPR_PSEUDO_32(eax, rax),
    DEFINE_GPR_PSEUDO_32(ebx, rbx),
    DEFINE_GPR_PSEUDO_32(ecx, rcx),
    DEFINE_GPR_PSEUDO_32(edx, rdx),
    DEFINE_GPR_PSEUDO_32(edi, rdi),
    DEFINE_GPR_PSEUDO_32(esi, rsi),
    DEFINE_GPR_PSEUDO_32(ebp, rbp),
    DEFINE_GPR_PSEUDO_32(esp, rsp),
    DEFINE_GPR_PSEUDO_32(r8d, r8),
    DEFINE_GPR_PSEUDO_32(r9d, r9),
    DEFINE_GPR_PSEUDO_32(r10d, r10),
    DEFINE_GPR_PSEUDO_32(r11d, r11),
    DEFINE_GPR_PSEUDO_32(r12d, r12),
    DEFINE_GPR_PSEUDO_32(r13d, r13),
    DEFINE_GPR_PSEUDO_32(r14d, r14),
    DEFINE_GPR_PSEUDO_32(r15d, r15),
    DEFINE_GPR_PSEUDO_16(ax, rax),
    DEFINE_GPR_PSEUDO_16(bx, rbx),
    DEFINE_GPR_PSEUDO_16(cx, rcx),
    DEFINE_GPR_PSEUDO_16(dx, rdx),
    DEFINE_GPR_PSEUDO_16(di, rdi),
    DEFINE_GPR_PSEUDO_16(si, rsi),
    DEFINE_GPR_PSEUDO_16(bp, rbp),
    DEFINE_GPR_PSEUDO_16(sp, rsp),
    DEFINE_GPR_PSEUDO_16(r8w, r8),
    DEFINE_GPR_PSEUDO_16(r9w, r9),
    DEFINE_GPR_PSEUDO_16(r10w, r10),
    DEFINE_GPR_PSEUDO_16(r11w, r11),
    DEFINE_GPR_PSEUDO_16(r12w, r12),
    DEFINE_GPR_PSEUDO_16(r13w, r13),
    DEFINE_GPR_PSEUDO_16(r14w, r14),
    DEFINE_GPR_PSEUDO_16(r15w, r15),
    DEFINE_GPR_PSEUDO_8H(ah, rax),
    DEFINE_GPR_PSEUDO_8H(bh, rbx),
    DEFINE_GPR_PSEUDO_8H(ch, rcx),
    DEFINE_GPR_PSEUDO_8H(dh, rdx),
    DEFINE_GPR_PSEUDO_8L(al, rax),
    DEFINE_GPR_PSEUDO_8L(bl, rbx),
    DEFINE_GPR_PSEUDO_8L(cl, rcx),
    DEFINE_GPR_PSEUDO_8L(dl, rdx),
    DEFINE_GPR_PSEUDO_8L(dil, rdi),
    DEFINE_GPR_PSEUDO_8L(sil, rsi),
    DEFINE_GPR_PSEUDO_8L(bpl, rbp),
    DEFINE_GPR_PSEUDO_8L(spl, rsp),
    DEFINE_GPR_PSEUDO_8L(r8l, r8),
    DEFINE_GPR_PSEUDO_8L(r9l, r9),
    DEFINE_GPR_PSEUDO_8L(r10l, r10),
    DEFINE_GPR_PSEUDO_8L(r11l, r11),
    DEFINE_GPR_PSEUDO_8L(r12l, r12),
    DEFINE_GPR_PSEUDO_8L(r13l, r13),
    DEFINE_GPR_PSEUDO_8L(r14l, r14),
    DEFINE_GPR_PSEUDO_8L(r15l, r15)};

// Floating point registers 64 bit
const DNBRegisterInfo DNBArchImplX86_64::g_fpu_registers_no_avx[] = {
    {e_regSetFPU, fpu_fcw, "fctrl", NULL, Uint, Hex, FPU_SIZE_UINT(fcw),
     FPU_OFFSET(fcw), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_fsw, "fstat", NULL, Uint, Hex, FPU_SIZE_UINT(fsw),
     FPU_OFFSET(fsw), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_ftw, "ftag", NULL, Uint, Hex, 2 /* sizeof __fpu_ftw + sizeof __fpu_rsrv1 */,
     FPU_OFFSET(ftw), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_fop, "fop", NULL, Uint, Hex, FPU_SIZE_UINT(fop),
     FPU_OFFSET(fop), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_ip, "fioff", NULL, Uint, Hex, FPU_SIZE_UINT(ip),
     FPU_OFFSET(ip), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_cs, "fiseg", NULL, Uint, Hex, FPU_SIZE_UINT(cs),
     FPU_OFFSET(cs), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_dp, "fooff", NULL, Uint, Hex, FPU_SIZE_UINT(dp),
     FPU_OFFSET(dp), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_ds, "foseg", NULL, Uint, Hex, FPU_SIZE_UINT(ds),
     FPU_OFFSET(ds), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_mxcsr, "mxcsr", NULL, Uint, Hex, FPU_SIZE_UINT(mxcsr),
     FPU_OFFSET(mxcsr), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_mxcsrmask, "mxcsrmask", NULL, Uint, Hex,
     FPU_SIZE_UINT(mxcsrmask), FPU_OFFSET(mxcsrmask), -1U, -1U, -1U, -1U, NULL,
     NULL},

    {e_regSetFPU, fpu_stmm0, "stmm0", "st0", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm0), FPU_OFFSET(stmm0), ehframe_dwarf_stmm0,
     ehframe_dwarf_stmm0, -1U, debugserver_stmm0, NULL, NULL},
    {e_regSetFPU, fpu_stmm1, "stmm1", "st1", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm1), FPU_OFFSET(stmm1), ehframe_dwarf_stmm1,
     ehframe_dwarf_stmm1, -1U, debugserver_stmm1, NULL, NULL},
    {e_regSetFPU, fpu_stmm2, "stmm2", "st2", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm2), FPU_OFFSET(stmm2), ehframe_dwarf_stmm2,
     ehframe_dwarf_stmm2, -1U, debugserver_stmm2, NULL, NULL},
    {e_regSetFPU, fpu_stmm3, "stmm3", "st3", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm3), FPU_OFFSET(stmm3), ehframe_dwarf_stmm3,
     ehframe_dwarf_stmm3, -1U, debugserver_stmm3, NULL, NULL},
    {e_regSetFPU, fpu_stmm4, "stmm4", "st4", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm4), FPU_OFFSET(stmm4), ehframe_dwarf_stmm4,
     ehframe_dwarf_stmm4, -1U, debugserver_stmm4, NULL, NULL},
    {e_regSetFPU, fpu_stmm5, "stmm5", "st5", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm5), FPU_OFFSET(stmm5), ehframe_dwarf_stmm5,
     ehframe_dwarf_stmm5, -1U, debugserver_stmm5, NULL, NULL},
    {e_regSetFPU, fpu_stmm6, "stmm6", "st6", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm6), FPU_OFFSET(stmm6), ehframe_dwarf_stmm6,
     ehframe_dwarf_stmm6, -1U, debugserver_stmm6, NULL, NULL},
    {e_regSetFPU, fpu_stmm7, "stmm7", "st7", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm7), FPU_OFFSET(stmm7), ehframe_dwarf_stmm7,
     ehframe_dwarf_stmm7, -1U, debugserver_stmm7, NULL, NULL},

    {e_regSetFPU, fpu_xmm0, "xmm0", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm0), FPU_OFFSET(xmm0), ehframe_dwarf_xmm0,
     ehframe_dwarf_xmm0, -1U, debugserver_xmm0, NULL, NULL},
    {e_regSetFPU, fpu_xmm1, "xmm1", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm1), FPU_OFFSET(xmm1), ehframe_dwarf_xmm1,
     ehframe_dwarf_xmm1, -1U, debugserver_xmm1, NULL, NULL},
    {e_regSetFPU, fpu_xmm2, "xmm2", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm2), FPU_OFFSET(xmm2), ehframe_dwarf_xmm2,
     ehframe_dwarf_xmm2, -1U, debugserver_xmm2, NULL, NULL},
    {e_regSetFPU, fpu_xmm3, "xmm3", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm3), FPU_OFFSET(xmm3), ehframe_dwarf_xmm3,
     ehframe_dwarf_xmm3, -1U, debugserver_xmm3, NULL, NULL},
    {e_regSetFPU, fpu_xmm4, "xmm4", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm4), FPU_OFFSET(xmm4), ehframe_dwarf_xmm4,
     ehframe_dwarf_xmm4, -1U, debugserver_xmm4, NULL, NULL},
    {e_regSetFPU, fpu_xmm5, "xmm5", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm5), FPU_OFFSET(xmm5), ehframe_dwarf_xmm5,
     ehframe_dwarf_xmm5, -1U, debugserver_xmm5, NULL, NULL},
    {e_regSetFPU, fpu_xmm6, "xmm6", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm6), FPU_OFFSET(xmm6), ehframe_dwarf_xmm6,
     ehframe_dwarf_xmm6, -1U, debugserver_xmm6, NULL, NULL},
    {e_regSetFPU, fpu_xmm7, "xmm7", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm7), FPU_OFFSET(xmm7), ehframe_dwarf_xmm7,
     ehframe_dwarf_xmm7, -1U, debugserver_xmm7, NULL, NULL},
    {e_regSetFPU, fpu_xmm8, "xmm8", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm8), FPU_OFFSET(xmm8), ehframe_dwarf_xmm8,
     ehframe_dwarf_xmm8, -1U, debugserver_xmm8, NULL, NULL},
    {e_regSetFPU, fpu_xmm9, "xmm9", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm9), FPU_OFFSET(xmm9), ehframe_dwarf_xmm9,
     ehframe_dwarf_xmm9, -1U, debugserver_xmm9, NULL, NULL},
    {e_regSetFPU, fpu_xmm10, "xmm10", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm10), FPU_OFFSET(xmm10), ehframe_dwarf_xmm10,
     ehframe_dwarf_xmm10, -1U, debugserver_xmm10, NULL, NULL},
    {e_regSetFPU, fpu_xmm11, "xmm11", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm11), FPU_OFFSET(xmm11), ehframe_dwarf_xmm11,
     ehframe_dwarf_xmm11, -1U, debugserver_xmm11, NULL, NULL},
    {e_regSetFPU, fpu_xmm12, "xmm12", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm12), FPU_OFFSET(xmm12), ehframe_dwarf_xmm12,
     ehframe_dwarf_xmm12, -1U, debugserver_xmm12, NULL, NULL},
    {e_regSetFPU, fpu_xmm13, "xmm13", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm13), FPU_OFFSET(xmm13), ehframe_dwarf_xmm13,
     ehframe_dwarf_xmm13, -1U, debugserver_xmm13, NULL, NULL},
    {e_regSetFPU, fpu_xmm14, "xmm14", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm14), FPU_OFFSET(xmm14), ehframe_dwarf_xmm14,
     ehframe_dwarf_xmm14, -1U, debugserver_xmm14, NULL, NULL},
    {e_regSetFPU, fpu_xmm15, "xmm15", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm15), FPU_OFFSET(xmm15), ehframe_dwarf_xmm15,
     ehframe_dwarf_xmm15, -1U, debugserver_xmm15, NULL, NULL},
};

static const char *g_contained_ymm0[] = {"ymm0", NULL};
static const char *g_contained_ymm1[] = {"ymm1", NULL};
static const char *g_contained_ymm2[] = {"ymm2", NULL};
static const char *g_contained_ymm3[] = {"ymm3", NULL};
static const char *g_contained_ymm4[] = {"ymm4", NULL};
static const char *g_contained_ymm5[] = {"ymm5", NULL};
static const char *g_contained_ymm6[] = {"ymm6", NULL};
static const char *g_contained_ymm7[] = {"ymm7", NULL};
static const char *g_contained_ymm8[] = {"ymm8", NULL};
static const char *g_contained_ymm9[] = {"ymm9", NULL};
static const char *g_contained_ymm10[] = {"ymm10", NULL};
static const char *g_contained_ymm11[] = {"ymm11", NULL};
static const char *g_contained_ymm12[] = {"ymm12", NULL};
static const char *g_contained_ymm13[] = {"ymm13", NULL};
static const char *g_contained_ymm14[] = {"ymm14", NULL};
static const char *g_contained_ymm15[] = {"ymm15", NULL};

const DNBRegisterInfo DNBArchImplX86_64::g_fpu_registers_avx[] = {
    {e_regSetFPU, fpu_fcw, "fctrl", NULL, Uint, Hex, FPU_SIZE_UINT(fcw),
     AVX_OFFSET(fcw), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_fsw, "fstat", NULL, Uint, Hex, FPU_SIZE_UINT(fsw),
     AVX_OFFSET(fsw), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_ftw, "ftag", NULL, Uint, Hex, 2 /* sizeof __fpu_ftw + sizeof __fpu_rsrv1 */,
     AVX_OFFSET(ftw), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_fop, "fop", NULL, Uint, Hex, FPU_SIZE_UINT(fop),
     AVX_OFFSET(fop), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_ip, "fioff", NULL, Uint, Hex, FPU_SIZE_UINT(ip),
     AVX_OFFSET(ip), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_cs, "fiseg", NULL, Uint, Hex, FPU_SIZE_UINT(cs),
     AVX_OFFSET(cs), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_dp, "fooff", NULL, Uint, Hex, FPU_SIZE_UINT(dp),
     AVX_OFFSET(dp), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_ds, "foseg", NULL, Uint, Hex, FPU_SIZE_UINT(ds),
     AVX_OFFSET(ds), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_mxcsr, "mxcsr", NULL, Uint, Hex, FPU_SIZE_UINT(mxcsr),
     AVX_OFFSET(mxcsr), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_mxcsrmask, "mxcsrmask", NULL, Uint, Hex,
     FPU_SIZE_UINT(mxcsrmask), AVX_OFFSET(mxcsrmask), -1U, -1U, -1U, -1U, NULL,
     NULL},

    {e_regSetFPU, fpu_stmm0, "stmm0", "st0", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm0), AVX_OFFSET(stmm0), ehframe_dwarf_stmm0,
     ehframe_dwarf_stmm0, -1U, debugserver_stmm0, NULL, NULL},
    {e_regSetFPU, fpu_stmm1, "stmm1", "st1", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm1), AVX_OFFSET(stmm1), ehframe_dwarf_stmm1,
     ehframe_dwarf_stmm1, -1U, debugserver_stmm1, NULL, NULL},
    {e_regSetFPU, fpu_stmm2, "stmm2", "st2", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm2), AVX_OFFSET(stmm2), ehframe_dwarf_stmm2,
     ehframe_dwarf_stmm2, -1U, debugserver_stmm2, NULL, NULL},
    {e_regSetFPU, fpu_stmm3, "stmm3", "st3", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm3), AVX_OFFSET(stmm3), ehframe_dwarf_stmm3,
     ehframe_dwarf_stmm3, -1U, debugserver_stmm3, NULL, NULL},
    {e_regSetFPU, fpu_stmm4, "stmm4", "st4", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm4), AVX_OFFSET(stmm4), ehframe_dwarf_stmm4,
     ehframe_dwarf_stmm4, -1U, debugserver_stmm4, NULL, NULL},
    {e_regSetFPU, fpu_stmm5, "stmm5", "st5", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm5), AVX_OFFSET(stmm5), ehframe_dwarf_stmm5,
     ehframe_dwarf_stmm5, -1U, debugserver_stmm5, NULL, NULL},
    {e_regSetFPU, fpu_stmm6, "stmm6", "st6", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm6), AVX_OFFSET(stmm6), ehframe_dwarf_stmm6,
     ehframe_dwarf_stmm6, -1U, debugserver_stmm6, NULL, NULL},
    {e_regSetFPU, fpu_stmm7, "stmm7", "st7", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm7), AVX_OFFSET(stmm7), ehframe_dwarf_stmm7,
     ehframe_dwarf_stmm7, -1U, debugserver_stmm7, NULL, NULL},

    {e_regSetFPU, fpu_ymm0, "ymm0", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm0), AVX_OFFSET_YMM(0), ehframe_dwarf_ymm0,
     ehframe_dwarf_ymm0, -1U, debugserver_ymm0, NULL, NULL},
    {e_regSetFPU, fpu_ymm1, "ymm1", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm1), AVX_OFFSET_YMM(1), ehframe_dwarf_ymm1,
     ehframe_dwarf_ymm1, -1U, debugserver_ymm1, NULL, NULL},
    {e_regSetFPU, fpu_ymm2, "ymm2", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm2), AVX_OFFSET_YMM(2), ehframe_dwarf_ymm2,
     ehframe_dwarf_ymm2, -1U, debugserver_ymm2, NULL, NULL},
    {e_regSetFPU, fpu_ymm3, "ymm3", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm3), AVX_OFFSET_YMM(3), ehframe_dwarf_ymm3,
     ehframe_dwarf_ymm3, -1U, debugserver_ymm3, NULL, NULL},
    {e_regSetFPU, fpu_ymm4, "ymm4", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm4), AVX_OFFSET_YMM(4), ehframe_dwarf_ymm4,
     ehframe_dwarf_ymm4, -1U, debugserver_ymm4, NULL, NULL},
    {e_regSetFPU, fpu_ymm5, "ymm5", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm5), AVX_OFFSET_YMM(5), ehframe_dwarf_ymm5,
     ehframe_dwarf_ymm5, -1U, debugserver_ymm5, NULL, NULL},
    {e_regSetFPU, fpu_ymm6, "ymm6", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm6), AVX_OFFSET_YMM(6), ehframe_dwarf_ymm6,
     ehframe_dwarf_ymm6, -1U, debugserver_ymm6, NULL, NULL},
    {e_regSetFPU, fpu_ymm7, "ymm7", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm7), AVX_OFFSET_YMM(7), ehframe_dwarf_ymm7,
     ehframe_dwarf_ymm7, -1U, debugserver_ymm7, NULL, NULL},
    {e_regSetFPU, fpu_ymm8, "ymm8", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm8), AVX_OFFSET_YMM(8), ehframe_dwarf_ymm8,
     ehframe_dwarf_ymm8, -1U, debugserver_ymm8, NULL, NULL},
    {e_regSetFPU, fpu_ymm9, "ymm9", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm9), AVX_OFFSET_YMM(9), ehframe_dwarf_ymm9,
     ehframe_dwarf_ymm9, -1U, debugserver_ymm9, NULL, NULL},
    {e_regSetFPU, fpu_ymm10, "ymm10", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm10), AVX_OFFSET_YMM(10), ehframe_dwarf_ymm10,
     ehframe_dwarf_ymm10, -1U, debugserver_ymm10, NULL, NULL},
    {e_regSetFPU, fpu_ymm11, "ymm11", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm11), AVX_OFFSET_YMM(11), ehframe_dwarf_ymm11,
     ehframe_dwarf_ymm11, -1U, debugserver_ymm11, NULL, NULL},
    {e_regSetFPU, fpu_ymm12, "ymm12", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm12), AVX_OFFSET_YMM(12), ehframe_dwarf_ymm12,
     ehframe_dwarf_ymm12, -1U, debugserver_ymm12, NULL, NULL},
    {e_regSetFPU, fpu_ymm13, "ymm13", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm13), AVX_OFFSET_YMM(13), ehframe_dwarf_ymm13,
     ehframe_dwarf_ymm13, -1U, debugserver_ymm13, NULL, NULL},
    {e_regSetFPU, fpu_ymm14, "ymm14", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm14), AVX_OFFSET_YMM(14), ehframe_dwarf_ymm14,
     ehframe_dwarf_ymm14, -1U, debugserver_ymm14, NULL, NULL},
    {e_regSetFPU, fpu_ymm15, "ymm15", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_YMM(ymm15), AVX_OFFSET_YMM(15), ehframe_dwarf_ymm15,
     ehframe_dwarf_ymm15, -1U, debugserver_ymm15, NULL, NULL},

    {e_regSetFPU, fpu_xmm0, "xmm0", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm0), 0, ehframe_dwarf_xmm0, ehframe_dwarf_xmm0, -1U,
     debugserver_xmm0, g_contained_ymm0, NULL},
    {e_regSetFPU, fpu_xmm1, "xmm1", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm1), 0, ehframe_dwarf_xmm1, ehframe_dwarf_xmm1, -1U,
     debugserver_xmm1, g_contained_ymm1, NULL},
    {e_regSetFPU, fpu_xmm2, "xmm2", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm2), 0, ehframe_dwarf_xmm2, ehframe_dwarf_xmm2, -1U,
     debugserver_xmm2, g_contained_ymm2, NULL},
    {e_regSetFPU, fpu_xmm3, "xmm3", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm3), 0, ehframe_dwarf_xmm3, ehframe_dwarf_xmm3, -1U,
     debugserver_xmm3, g_contained_ymm3, NULL},
    {e_regSetFPU, fpu_xmm4, "xmm4", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm4), 0, ehframe_dwarf_xmm4, ehframe_dwarf_xmm4, -1U,
     debugserver_xmm4, g_contained_ymm4, NULL},
    {e_regSetFPU, fpu_xmm5, "xmm5", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm5), 0, ehframe_dwarf_xmm5, ehframe_dwarf_xmm5, -1U,
     debugserver_xmm5, g_contained_ymm5, NULL},
    {e_regSetFPU, fpu_xmm6, "xmm6", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm6), 0, ehframe_dwarf_xmm6, ehframe_dwarf_xmm6, -1U,
     debugserver_xmm6, g_contained_ymm6, NULL},
    {e_regSetFPU, fpu_xmm7, "xmm7", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm7), 0, ehframe_dwarf_xmm7, ehframe_dwarf_xmm7, -1U,
     debugserver_xmm7, g_contained_ymm7, NULL},
    {e_regSetFPU, fpu_xmm8, "xmm8", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm8), 0, ehframe_dwarf_xmm8, ehframe_dwarf_xmm8, -1U,
     debugserver_xmm8, g_contained_ymm8, NULL},
    {e_regSetFPU, fpu_xmm9, "xmm9", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm9), 0, ehframe_dwarf_xmm9, ehframe_dwarf_xmm9, -1U,
     debugserver_xmm9, g_contained_ymm9, NULL},
    {e_regSetFPU, fpu_xmm10, "xmm10", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm10), 0, ehframe_dwarf_xmm10, ehframe_dwarf_xmm10, -1U,
     debugserver_xmm10, g_contained_ymm10, NULL},
    {e_regSetFPU, fpu_xmm11, "xmm11", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm11), 0, ehframe_dwarf_xmm11, ehframe_dwarf_xmm11, -1U,
     debugserver_xmm11, g_contained_ymm11, NULL},
    {e_regSetFPU, fpu_xmm12, "xmm12", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm12), 0, ehframe_dwarf_xmm12, ehframe_dwarf_xmm12, -1U,
     debugserver_xmm12, g_contained_ymm12, NULL},
    {e_regSetFPU, fpu_xmm13, "xmm13", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm13), 0, ehframe_dwarf_xmm13, ehframe_dwarf_xmm13, -1U,
     debugserver_xmm13, g_contained_ymm13, NULL},
    {e_regSetFPU, fpu_xmm14, "xmm14", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm14), 0, ehframe_dwarf_xmm14, ehframe_dwarf_xmm14, -1U,
     debugserver_xmm14, g_contained_ymm14, NULL},
    {e_regSetFPU, fpu_xmm15, "xmm15", NULL, Vector, VectorOfUInt8,
     FPU_SIZE_XMM(xmm15), 0, ehframe_dwarf_xmm15, ehframe_dwarf_xmm15, -1U,
     debugserver_xmm15, g_contained_ymm15, NULL}

};

static const char *g_contained_zmm0[] = {"zmm0", NULL};
static const char *g_contained_zmm1[] = {"zmm1", NULL};
static const char *g_contained_zmm2[] = {"zmm2", NULL};
static const char *g_contained_zmm3[] = {"zmm3", NULL};
static const char *g_contained_zmm4[] = {"zmm4", NULL};
static const char *g_contained_zmm5[] = {"zmm5", NULL};
static const char *g_contained_zmm6[] = {"zmm6", NULL};
static const char *g_contained_zmm7[] = {"zmm7", NULL};
static const char *g_contained_zmm8[] = {"zmm8", NULL};
static const char *g_contained_zmm9[] = {"zmm9", NULL};
static const char *g_contained_zmm10[] = {"zmm10", NULL};
static const char *g_contained_zmm11[] = {"zmm11", NULL};
static const char *g_contained_zmm12[] = {"zmm12", NULL};
static const char *g_contained_zmm13[] = {"zmm13", NULL};
static const char *g_contained_zmm14[] = {"zmm14", NULL};
static const char *g_contained_zmm15[] = {"zmm15", NULL};

#define STR(s) #s

#define ZMM_REG_DEF(reg)                                                       \
  {                                                                            \
    e_regSetFPU, fpu_zmm##reg,  STR(zmm##reg), NULL, Vector, VectorOfUInt8,    \
        FPU_SIZE_ZMM(zmm##reg), AVX512F_OFFSET_ZMM(reg),                       \
        ehframe_dwarf_zmm##reg, ehframe_dwarf_zmm##reg, -1U,                   \
        debugserver_zmm##reg, NULL, NULL                                       \
  }

#define YMM_REG_ALIAS(reg)                                                     \
  {                                                                            \
    e_regSetFPU, fpu_ymm##reg, STR(ymm##reg), NULL, Vector, VectorOfUInt8,     \
        FPU_SIZE_YMM(ymm##reg), 0, ehframe_dwarf_ymm##reg,                     \
        ehframe_dwarf_ymm##reg, -1U, debugserver_ymm##reg,                     \
        g_contained_zmm##reg, NULL                                             \
  }

#define XMM_REG_ALIAS(reg)                                                     \
  {                                                                            \
    e_regSetFPU, fpu_xmm##reg,  STR(xmm##reg), NULL, Vector, VectorOfUInt8,    \
        FPU_SIZE_XMM(xmm##reg), 0, ehframe_dwarf_xmm##reg,                     \
        ehframe_dwarf_xmm##reg, -1U, debugserver_xmm##reg,                     \
        g_contained_zmm##reg, NULL                                             \
  }

#define AVX512_K_REG_DEF(reg)                                                  \
  {                                                                            \
    e_regSetFPU, fpu_k##reg, STR(k##reg), NULL, Vector, VectorOfUInt8, 8,      \
        AVX512F_OFFSET(k##reg), ehframe_dwarf_k##reg, ehframe_dwarf_k##reg,    \
        -1U, debugserver_k##reg, NULL, NULL                                    \
  }

const DNBRegisterInfo DNBArchImplX86_64::g_fpu_registers_avx512f[] = {
    {e_regSetFPU, fpu_fcw, "fctrl", NULL, Uint, Hex, FPU_SIZE_UINT(fcw),
     AVX_OFFSET(fcw), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_fsw, "fstat", NULL, Uint, Hex, FPU_SIZE_UINT(fsw),
     AVX_OFFSET(fsw), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_ftw, "ftag", NULL, Uint, Hex, 2 /* sizeof __fpu_ftw + sizeof __fpu_rsrv1 */,
     AVX_OFFSET(ftw), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_fop, "fop", NULL, Uint, Hex, FPU_SIZE_UINT(fop),
     AVX_OFFSET(fop), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_ip, "fioff", NULL, Uint, Hex, FPU_SIZE_UINT(ip),
     AVX_OFFSET(ip), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_cs, "fiseg", NULL, Uint, Hex, FPU_SIZE_UINT(cs),
     AVX_OFFSET(cs), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_dp, "fooff", NULL, Uint, Hex, FPU_SIZE_UINT(dp),
     AVX_OFFSET(dp), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_ds, "foseg", NULL, Uint, Hex, FPU_SIZE_UINT(ds),
     AVX_OFFSET(ds), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_mxcsr, "mxcsr", NULL, Uint, Hex, FPU_SIZE_UINT(mxcsr),
     AVX_OFFSET(mxcsr), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetFPU, fpu_mxcsrmask, "mxcsrmask", NULL, Uint, Hex,
     FPU_SIZE_UINT(mxcsrmask), AVX_OFFSET(mxcsrmask), -1U, -1U, -1U, -1U, NULL,
     NULL},

    {e_regSetFPU, fpu_stmm0, "stmm0", "st0", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm0), AVX_OFFSET(stmm0), ehframe_dwarf_stmm0,
     ehframe_dwarf_stmm0, -1U, debugserver_stmm0, NULL, NULL},
    {e_regSetFPU, fpu_stmm1, "stmm1", "st1", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm1), AVX_OFFSET(stmm1), ehframe_dwarf_stmm1,
     ehframe_dwarf_stmm1, -1U, debugserver_stmm1, NULL, NULL},
    {e_regSetFPU, fpu_stmm2, "stmm2", "st2", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm2), AVX_OFFSET(stmm2), ehframe_dwarf_stmm2,
     ehframe_dwarf_stmm2, -1U, debugserver_stmm2, NULL, NULL},
    {e_regSetFPU, fpu_stmm3, "stmm3", "st3", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm3), AVX_OFFSET(stmm3), ehframe_dwarf_stmm3,
     ehframe_dwarf_stmm3, -1U, debugserver_stmm3, NULL, NULL},
    {e_regSetFPU, fpu_stmm4, "stmm4", "st4", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm4), AVX_OFFSET(stmm4), ehframe_dwarf_stmm4,
     ehframe_dwarf_stmm4, -1U, debugserver_stmm4, NULL, NULL},
    {e_regSetFPU, fpu_stmm5, "stmm5", "st5", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm5), AVX_OFFSET(stmm5), ehframe_dwarf_stmm5,
     ehframe_dwarf_stmm5, -1U, debugserver_stmm5, NULL, NULL},
    {e_regSetFPU, fpu_stmm6, "stmm6", "st6", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm6), AVX_OFFSET(stmm6), ehframe_dwarf_stmm6,
     ehframe_dwarf_stmm6, -1U, debugserver_stmm6, NULL, NULL},
    {e_regSetFPU, fpu_stmm7, "stmm7", "st7", Vector, VectorOfUInt8,
     FPU_SIZE_MMST(stmm7), AVX_OFFSET(stmm7), ehframe_dwarf_stmm7,
     ehframe_dwarf_stmm7, -1U, debugserver_stmm7, NULL, NULL},

     AVX512_K_REG_DEF(0),
     AVX512_K_REG_DEF(1),
     AVX512_K_REG_DEF(2),
     AVX512_K_REG_DEF(3),
     AVX512_K_REG_DEF(4),
     AVX512_K_REG_DEF(5),
     AVX512_K_REG_DEF(6),
     AVX512_K_REG_DEF(7),

     ZMM_REG_DEF(0),
     ZMM_REG_DEF(1),
     ZMM_REG_DEF(2),
     ZMM_REG_DEF(3),
     ZMM_REG_DEF(4),
     ZMM_REG_DEF(5),
     ZMM_REG_DEF(6),
     ZMM_REG_DEF(7),
     ZMM_REG_DEF(8),
     ZMM_REG_DEF(9),
     ZMM_REG_DEF(10),
     ZMM_REG_DEF(11),
     ZMM_REG_DEF(12),
     ZMM_REG_DEF(13),
     ZMM_REG_DEF(14),
     ZMM_REG_DEF(15),
     ZMM_REG_DEF(16),
     ZMM_REG_DEF(17),
     ZMM_REG_DEF(18),
     ZMM_REG_DEF(19),
     ZMM_REG_DEF(20),
     ZMM_REG_DEF(21),
     ZMM_REG_DEF(22),
     ZMM_REG_DEF(23),
     ZMM_REG_DEF(24),
     ZMM_REG_DEF(25),
     ZMM_REG_DEF(26),
     ZMM_REG_DEF(27),
     ZMM_REG_DEF(28),
     ZMM_REG_DEF(29),
     ZMM_REG_DEF(30),
     ZMM_REG_DEF(31),

     YMM_REG_ALIAS(0),
     YMM_REG_ALIAS(1),
     YMM_REG_ALIAS(2),
     YMM_REG_ALIAS(3),
     YMM_REG_ALIAS(4),
     YMM_REG_ALIAS(5),
     YMM_REG_ALIAS(6),
     YMM_REG_ALIAS(7),
     YMM_REG_ALIAS(8),
     YMM_REG_ALIAS(9),
     YMM_REG_ALIAS(10),
     YMM_REG_ALIAS(11),
     YMM_REG_ALIAS(12),
     YMM_REG_ALIAS(13),
     YMM_REG_ALIAS(14),
     YMM_REG_ALIAS(15),

     XMM_REG_ALIAS(0),
     XMM_REG_ALIAS(1),
     XMM_REG_ALIAS(2),
     XMM_REG_ALIAS(3),
     XMM_REG_ALIAS(4),
     XMM_REG_ALIAS(5),
     XMM_REG_ALIAS(6),
     XMM_REG_ALIAS(7),
     XMM_REG_ALIAS(8),
     XMM_REG_ALIAS(9),
     XMM_REG_ALIAS(10),
     XMM_REG_ALIAS(11),
     XMM_REG_ALIAS(12),
     XMM_REG_ALIAS(13),
     XMM_REG_ALIAS(14),
     XMM_REG_ALIAS(15),

};


// Exception registers

const DNBRegisterInfo DNBArchImplX86_64::g_exc_registers[] = {
    {e_regSetEXC, exc_trapno, "trapno", NULL, Uint, Hex, EXC_SIZE(trapno),
     EXC_OFFSET(trapno), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetEXC, exc_err, "err", NULL, Uint, Hex, EXC_SIZE(err),
     EXC_OFFSET(err), -1U, -1U, -1U, -1U, NULL, NULL},
    {e_regSetEXC, exc_faultvaddr, "faultvaddr", NULL, Uint, Hex,
     EXC_SIZE(faultvaddr), EXC_OFFSET(faultvaddr), -1U, -1U, -1U, -1U, NULL,
     NULL}};

// Number of registers in each register set
const size_t DNBArchImplX86_64::k_num_gpr_registers =
    sizeof(g_gpr_registers) / sizeof(DNBRegisterInfo);
const size_t DNBArchImplX86_64::k_num_fpu_registers_no_avx =
    sizeof(g_fpu_registers_no_avx) / sizeof(DNBRegisterInfo);
const size_t DNBArchImplX86_64::k_num_fpu_registers_avx =
    sizeof(g_fpu_registers_avx) / sizeof(DNBRegisterInfo);
const size_t DNBArchImplX86_64::k_num_exc_registers =
    sizeof(g_exc_registers) / sizeof(DNBRegisterInfo);
const size_t DNBArchImplX86_64::k_num_all_registers_no_avx =
    k_num_gpr_registers + k_num_fpu_registers_no_avx + k_num_exc_registers;
const size_t DNBArchImplX86_64::k_num_all_registers_avx =
    k_num_gpr_registers + k_num_fpu_registers_avx + k_num_exc_registers;
const size_t DNBArchImplX86_64::k_num_fpu_registers_avx512f =
    sizeof(g_fpu_registers_avx512f) / sizeof(DNBRegisterInfo);
const size_t DNBArchImplX86_64::k_num_all_registers_avx512f =
    k_num_gpr_registers + k_num_fpu_registers_avx512f + k_num_exc_registers;

// Register set definitions. The first definitions at register set index
// of zero is for all registers, followed by other registers sets. The
// register information for the all register set need not be filled in.
const DNBRegisterSetInfo DNBArchImplX86_64::g_reg_sets_no_avx[] = {
    {"x86_64 Registers", NULL, k_num_all_registers_no_avx},
    {"General Purpose Registers", g_gpr_registers, k_num_gpr_registers},
    {"Floating Point Registers", g_fpu_registers_no_avx,
     k_num_fpu_registers_no_avx},
    {"Exception State Registers", g_exc_registers, k_num_exc_registers}};

const DNBRegisterSetInfo DNBArchImplX86_64::g_reg_sets_avx[] = {
    {"x86_64 Registers", NULL, k_num_all_registers_avx},
    {"General Purpose Registers", g_gpr_registers, k_num_gpr_registers},
    {"Floating Point Registers", g_fpu_registers_avx, k_num_fpu_registers_avx},
    {"Exception State Registers", g_exc_registers, k_num_exc_registers}};

const DNBRegisterSetInfo DNBArchImplX86_64::g_reg_sets_avx512f[] = {
    {"x86_64 Registers", NULL, k_num_all_registers_avx},
    {"General Purpose Registers", g_gpr_registers, k_num_gpr_registers},
    {"Floating Point Registers", g_fpu_registers_avx512f,
     k_num_fpu_registers_avx512f},
    {"Exception State Registers", g_exc_registers, k_num_exc_registers}};

// Total number of register sets for this architecture
const size_t DNBArchImplX86_64::k_num_register_sets =
    sizeof(g_reg_sets_avx) / sizeof(DNBRegisterSetInfo);

DNBArchProtocol *DNBArchImplX86_64::Create(MachThread *thread) {
  DNBArchImplX86_64 *obj = new DNBArchImplX86_64(thread);
  return obj;
}

const uint8_t *
DNBArchImplX86_64::SoftwareBreakpointOpcode(nub_size_t byte_size) {
  static const uint8_t g_breakpoint_opcode[] = {0xCC};
  if (byte_size == 1)
    return g_breakpoint_opcode;
  return NULL;
}

const DNBRegisterSetInfo *
DNBArchImplX86_64::GetRegisterSetInfo(nub_size_t *num_reg_sets) {
  *num_reg_sets = k_num_register_sets;

  if (CPUHasAVX512f() || FORCE_AVX_REGS)
    return g_reg_sets_avx512f;
  if (CPUHasAVX() || FORCE_AVX_REGS)
    return g_reg_sets_avx;
  else
    return g_reg_sets_no_avx;
}

void DNBArchImplX86_64::Initialize() {
  DNBArchPluginInfo arch_plugin_info = {
      CPU_TYPE_X86_64, DNBArchImplX86_64::Create,
      DNBArchImplX86_64::GetRegisterSetInfo,
      DNBArchImplX86_64::SoftwareBreakpointOpcode};

  // Register this arch plug-in with the main protocol class
  DNBArchProtocol::RegisterArchPlugin(arch_plugin_info);
}

bool DNBArchImplX86_64::GetRegisterValue(uint32_t set, uint32_t reg,
                                         DNBRegisterValue *value) {
  if (set == REGISTER_SET_GENERIC) {
    switch (reg) {
    case GENERIC_REGNUM_PC: // Program Counter
      set = e_regSetGPR;
      reg = gpr_rip;
      break;

    case GENERIC_REGNUM_SP: // Stack Pointer
      set = e_regSetGPR;
      reg = gpr_rsp;
      break;

    case GENERIC_REGNUM_FP: // Frame Pointer
      set = e_regSetGPR;
      reg = gpr_rbp;
      break;

    case GENERIC_REGNUM_FLAGS: // Processor flags register
      set = e_regSetGPR;
      reg = gpr_rflags;
      break;

    case GENERIC_REGNUM_RA: // Return Address
    default:
      return false;
    }
  }

  if (GetRegisterState(set, false) != KERN_SUCCESS)
    return false;

  const DNBRegisterInfo *regInfo = m_thread->GetRegisterInfo(set, reg);
  if (regInfo) {
    value->info = *regInfo;
    switch (set) {
    case e_regSetGPR:
      if (reg < k_num_gpr_registers) {
        value->value.uint64 = ((uint64_t *)(&m_state.context.gpr))[reg];
        return true;
      }
      break;

    case e_regSetFPU:
      if (reg > fpu_xmm15 && !(CPUHasAVX() || FORCE_AVX_REGS))
        return false;
      if (reg > fpu_ymm15 && !(CPUHasAVX512f() || FORCE_AVX_REGS))
        return false;
      switch (reg) {

      case fpu_fcw:
        value->value.uint16 =
            *((uint16_t *)(&m_state.context.fpu.no_avx.__fpu_fcw));
        return true;
      case fpu_fsw:
        value->value.uint16 =
            *((uint16_t *)(&m_state.context.fpu.no_avx.__fpu_fsw));
        return true;
      case fpu_ftw:
        memcpy (&value->value.uint16, &m_state.context.fpu.no_avx.__fpu_ftw, 2);
        return true;
      case fpu_fop:
        value->value.uint16 = m_state.context.fpu.no_avx.__fpu_fop;
        return true;
      case fpu_ip:
        value->value.uint32 = m_state.context.fpu.no_avx.__fpu_ip;
        return true;
      case fpu_cs:
        value->value.uint16 = m_state.context.fpu.no_avx.__fpu_cs;
        return true;
      case fpu_dp:
        value->value.uint32 = m_state.context.fpu.no_avx.__fpu_dp;
        return true;
      case fpu_ds:
        value->value.uint16 = m_state.context.fpu.no_avx.__fpu_ds;
        return true;
      case fpu_mxcsr:
        value->value.uint32 = m_state.context.fpu.no_avx.__fpu_mxcsr;
        return true;
      case fpu_mxcsrmask:
        value->value.uint32 = m_state.context.fpu.no_avx.__fpu_mxcsrmask;
        return true;

      case fpu_stmm0:
      case fpu_stmm1:
      case fpu_stmm2:
      case fpu_stmm3:
      case fpu_stmm4:
      case fpu_stmm5:
      case fpu_stmm6:
      case fpu_stmm7:
        memcpy(&value->value.uint8,
               &m_state.context.fpu.no_avx.__fpu_stmm0 + (reg - fpu_stmm0), 10);
        return true;

      case fpu_xmm0:
      case fpu_xmm1:
      case fpu_xmm2:
      case fpu_xmm3:
      case fpu_xmm4:
      case fpu_xmm5:
      case fpu_xmm6:
      case fpu_xmm7:
      case fpu_xmm8:
      case fpu_xmm9:
      case fpu_xmm10:
      case fpu_xmm11:
      case fpu_xmm12:
      case fpu_xmm13:
      case fpu_xmm14:
      case fpu_xmm15:
        memcpy(&value->value.uint8,
               &m_state.context.fpu.no_avx.__fpu_xmm0 + (reg - fpu_xmm0), 16);
        return true;

      case fpu_ymm0:
      case fpu_ymm1:
      case fpu_ymm2:
      case fpu_ymm3:
      case fpu_ymm4:
      case fpu_ymm5:
      case fpu_ymm6:
      case fpu_ymm7:
      case fpu_ymm8:
      case fpu_ymm9:
      case fpu_ymm10:
      case fpu_ymm11:
      case fpu_ymm12:
      case fpu_ymm13:
      case fpu_ymm14:
      case fpu_ymm15:
        memcpy(&value->value.uint8,
               &m_state.context.fpu.avx.__fpu_xmm0 + (reg - fpu_ymm0), 16);
        memcpy((&value->value.uint8) + 16,
               &m_state.context.fpu.avx.__fpu_ymmh0 + (reg - fpu_ymm0), 16);
        return true;
      case fpu_k0:
      case fpu_k1:
      case fpu_k2:
      case fpu_k3:
      case fpu_k4:
      case fpu_k5:
      case fpu_k6:
      case fpu_k7:
        memcpy((&value->value.uint8),
               &m_state.context.fpu.avx512f.__fpu_k0 + (reg - fpu_k0), 8);
        return true;
      case fpu_zmm0:
      case fpu_zmm1:
      case fpu_zmm2:
      case fpu_zmm3:
      case fpu_zmm4:
      case fpu_zmm5:
      case fpu_zmm6:
      case fpu_zmm7:
      case fpu_zmm8:
      case fpu_zmm9:
      case fpu_zmm10:
      case fpu_zmm11:
      case fpu_zmm12:
      case fpu_zmm13:
      case fpu_zmm14:
      case fpu_zmm15:
        memcpy(&value->value.uint8,
               &m_state.context.fpu.avx512f.__fpu_xmm0 + (reg - fpu_zmm0), 16);
        memcpy((&value->value.uint8) + 16,
               &m_state.context.fpu.avx512f.__fpu_ymmh0 + (reg - fpu_zmm0), 16);
        memcpy((&value->value.uint8) + 32,
               &m_state.context.fpu.avx512f.__fpu_zmmh0 + (reg - fpu_zmm0), 32);
        return true;
      case fpu_zmm16:
      case fpu_zmm17:
      case fpu_zmm18:
      case fpu_zmm19:
      case fpu_zmm20:
      case fpu_zmm21:
      case fpu_zmm22:
      case fpu_zmm23:
      case fpu_zmm24:
      case fpu_zmm25:
      case fpu_zmm26:
      case fpu_zmm27:
      case fpu_zmm28:
      case fpu_zmm29:
      case fpu_zmm30:
      case fpu_zmm31:
        memcpy(&value->value.uint8,
               &m_state.context.fpu.avx512f.__fpu_zmm16 + (reg - fpu_zmm16), 64);
        return true;
      }
      break;

    case e_regSetEXC:
      switch (reg) {
      case exc_trapno:
        value->value.uint32 = m_state.context.exc.__trapno;
        return true;
      case exc_err:
        value->value.uint32 = m_state.context.exc.__err;
        return true;
      case exc_faultvaddr:
        value->value.uint64 = m_state.context.exc.__faultvaddr;
        return true;
      }
      break;
    }
  }
  return false;
}

bool DNBArchImplX86_64::SetRegisterValue(uint32_t set, uint32_t reg,
                                         const DNBRegisterValue *value) {
  if (set == REGISTER_SET_GENERIC) {
    switch (reg) {
    case GENERIC_REGNUM_PC: // Program Counter
      set = e_regSetGPR;
      reg = gpr_rip;
      break;

    case GENERIC_REGNUM_SP: // Stack Pointer
      set = e_regSetGPR;
      reg = gpr_rsp;
      break;

    case GENERIC_REGNUM_FP: // Frame Pointer
      set = e_regSetGPR;
      reg = gpr_rbp;
      break;

    case GENERIC_REGNUM_FLAGS: // Processor flags register
      set = e_regSetGPR;
      reg = gpr_rflags;
      break;

    case GENERIC_REGNUM_RA: // Return Address
    default:
      return false;
    }
  }

  if (GetRegisterState(set, false) != KERN_SUCCESS)
    return false;

  bool success = false;
  const DNBRegisterInfo *regInfo = m_thread->GetRegisterInfo(set, reg);
  if (regInfo) {
    switch (set) {
    case e_regSetGPR:
      if (reg < k_num_gpr_registers) {
        ((uint64_t *)(&m_state.context.gpr))[reg] = value->value.uint64;
        success = true;
      }
      break;
      if (reg > fpu_xmm15 && !(CPUHasAVX() || FORCE_AVX_REGS))
        return false;
      if (reg > fpu_ymm15 && !(CPUHasAVX512f() || FORCE_AVX_REGS))
        return false;
    case e_regSetFPU:
      switch (reg) {
      case fpu_fcw:
        *((uint16_t *)(&m_state.context.fpu.no_avx.__fpu_fcw)) =
            value->value.uint16;
        success = true;
        break;
      case fpu_fsw:
        *((uint16_t *)(&m_state.context.fpu.no_avx.__fpu_fsw)) =
            value->value.uint16;
        success = true;
        break;
      case fpu_ftw:
        memcpy (&m_state.context.fpu.no_avx.__fpu_ftw, &value->value.uint8, 2);
        success = true;
        break;
      case fpu_fop:
        m_state.context.fpu.no_avx.__fpu_fop = value->value.uint16;
        success = true;
        break;
      case fpu_ip:
        m_state.context.fpu.no_avx.__fpu_ip = value->value.uint32;
        success = true;
        break;
      case fpu_cs:
        m_state.context.fpu.no_avx.__fpu_cs = value->value.uint16;
        success = true;
        break;
      case fpu_dp:
        m_state.context.fpu.no_avx.__fpu_dp = value->value.uint32;
        success = true;
        break;
      case fpu_ds:
        m_state.context.fpu.no_avx.__fpu_ds = value->value.uint16;
        success = true;
        break;
      case fpu_mxcsr:
        m_state.context.fpu.no_avx.__fpu_mxcsr = value->value.uint32;
        success = true;
        break;
      case fpu_mxcsrmask:
        m_state.context.fpu.no_avx.__fpu_mxcsrmask = value->value.uint32;
        success = true;
        break;

      case fpu_stmm0:
      case fpu_stmm1:
      case fpu_stmm2:
      case fpu_stmm3:
      case fpu_stmm4:
      case fpu_stmm5:
      case fpu_stmm6:
      case fpu_stmm7:
        memcpy(&m_state.context.fpu.no_avx.__fpu_stmm0 + (reg - fpu_stmm0),
               &value->value.uint8, 10);
        success = true;
        break;

      case fpu_xmm0:
      case fpu_xmm1:
      case fpu_xmm2:
      case fpu_xmm3:
      case fpu_xmm4:
      case fpu_xmm5:
      case fpu_xmm6:
      case fpu_xmm7:
      case fpu_xmm8:
      case fpu_xmm9:
      case fpu_xmm10:
      case fpu_xmm11:
      case fpu_xmm12:
      case fpu_xmm13:
      case fpu_xmm14:
      case fpu_xmm15:
        memcpy(&m_state.context.fpu.no_avx.__fpu_xmm0 + (reg - fpu_xmm0),
               &value->value.uint8, 16);
        success = true;
        break;

      case fpu_ymm0:
      case fpu_ymm1:
      case fpu_ymm2:
      case fpu_ymm3:
      case fpu_ymm4:
      case fpu_ymm5:
      case fpu_ymm6:
      case fpu_ymm7:
      case fpu_ymm8:
      case fpu_ymm9:
      case fpu_ymm10:
      case fpu_ymm11:
      case fpu_ymm12:
      case fpu_ymm13:
      case fpu_ymm14:
      case fpu_ymm15:
        memcpy(&m_state.context.fpu.avx.__fpu_xmm0 + (reg - fpu_ymm0),
               &value->value.uint8, 16);
        memcpy(&m_state.context.fpu.avx.__fpu_ymmh0 + (reg - fpu_ymm0),
               (&value->value.uint8) + 16, 16);
        success = true;
        break;
      case fpu_k0:
      case fpu_k1:
      case fpu_k2:
      case fpu_k3:
      case fpu_k4:
      case fpu_k5:
      case fpu_k6:
      case fpu_k7:
        memcpy(&m_state.context.fpu.avx512f.__fpu_k0 + (reg - fpu_k0),
               &value->value.uint8, 8);
        success = true;
        break;
      case fpu_zmm0:
      case fpu_zmm1:
      case fpu_zmm2:
      case fpu_zmm3:
      case fpu_zmm4:
      case fpu_zmm5:
      case fpu_zmm6:
      case fpu_zmm7:
      case fpu_zmm8:
      case fpu_zmm9:
      case fpu_zmm10:
      case fpu_zmm11:
      case fpu_zmm12:
      case fpu_zmm13:
      case fpu_zmm14:
      case fpu_zmm15:
        memcpy(&m_state.context.fpu.avx512f.__fpu_xmm0 + (reg - fpu_zmm0),
               &value->value.uint8, 16);
        memcpy(&m_state.context.fpu.avx512f.__fpu_ymmh0 + (reg - fpu_zmm0),
               &value->value.uint8 + 16, 16);
        memcpy(&m_state.context.fpu.avx512f.__fpu_zmmh0 + (reg - fpu_zmm0),
               &value->value.uint8 + 32, 32);
        success = true;
        break;
      case fpu_zmm16:
      case fpu_zmm17:
      case fpu_zmm18:
      case fpu_zmm19:
      case fpu_zmm20:
      case fpu_zmm21:
      case fpu_zmm22:
      case fpu_zmm23:
      case fpu_zmm24:
      case fpu_zmm25:
      case fpu_zmm26:
      case fpu_zmm27:
      case fpu_zmm28:
      case fpu_zmm29:
      case fpu_zmm30:
      case fpu_zmm31:
        memcpy(&m_state.context.fpu.avx512f.__fpu_zmm16 + (reg - fpu_zmm16),
               &value->value.uint8, 64);
        success = true;
        break;
      }
      break;

    case e_regSetEXC:
      switch (reg) {
      case exc_trapno:
        m_state.context.exc.__trapno = value->value.uint32;
        success = true;
        break;
      case exc_err:
        m_state.context.exc.__err = value->value.uint32;
        success = true;
        break;
      case exc_faultvaddr:
        m_state.context.exc.__faultvaddr = value->value.uint64;
        success = true;
        break;
      }
      break;
    }
  }

  if (success)
    return SetRegisterState(set) == KERN_SUCCESS;
  return false;
}

uint32_t DNBArchImplX86_64::GetRegisterContextSize() {
  static uint32_t g_cached_size = 0;
  if (g_cached_size == 0) {
    if (CPUHasAVX512f() || FORCE_AVX_REGS) {
      for (size_t i = 0; i < k_num_fpu_registers_avx512f; ++i) {
        if (g_fpu_registers_avx512f[i].value_regs == NULL)
          g_cached_size += g_fpu_registers_avx512f[i].size;
      }
    } else if (CPUHasAVX() || FORCE_AVX_REGS) {
      for (size_t i = 0; i < k_num_fpu_registers_avx; ++i) {
        if (g_fpu_registers_avx[i].value_regs == NULL)
          g_cached_size += g_fpu_registers_avx[i].size;
      }
    } else {
      for (size_t i = 0; i < k_num_fpu_registers_no_avx; ++i) {
        if (g_fpu_registers_no_avx[i].value_regs == NULL)
          g_cached_size += g_fpu_registers_no_avx[i].size;
      }
    }
    DNBLogThreaded("DNBArchImplX86_64::GetRegisterContextSize() - GPR = %zu, "
                   "FPU = %u, EXC = %zu",
                   sizeof(GPR), g_cached_size, sizeof(EXC));
    g_cached_size += sizeof(GPR);
    g_cached_size += sizeof(EXC);
    DNBLogThreaded(
        "DNBArchImplX86_64::GetRegisterContextSize() - GPR + FPU + EXC = %u",
        g_cached_size);
  }
  return g_cached_size;
}

nub_size_t DNBArchImplX86_64::GetRegisterContext(void *buf,
                                                 nub_size_t buf_len) {
  uint32_t size = GetRegisterContextSize();

  if (buf && buf_len) {
    bool force = false;
    kern_return_t kret;

    if ((kret = GetGPRState(force)) != KERN_SUCCESS) {
      DNBLogThreadedIf(LOG_THREAD, "DNBArchImplX86_64::GetRegisterContext (buf "
                                   "= %p, len = %llu) error: GPR regs failed "
                                   "to read: %u ",
                       buf, (uint64_t)buf_len, kret);
      size = 0;
    } else if ((kret = GetFPUState(force)) != KERN_SUCCESS) {
      DNBLogThreadedIf(
          LOG_THREAD, "DNBArchImplX86_64::GetRegisterContext (buf = %p, len = "
                      "%llu) error: %s regs failed to read: %u",
          buf, (uint64_t)buf_len, CPUHasAVX() ? "AVX" : "FPU", kret);
      size = 0;
    } else if ((kret = GetEXCState(force)) != KERN_SUCCESS) {
      DNBLogThreadedIf(LOG_THREAD, "DNBArchImplX86_64::GetRegisterContext (buf "
                                   "= %p, len = %llu) error: EXC regs failed "
                                   "to read: %u",
                       buf, (uint64_t)buf_len, kret);
      size = 0;
    } else {
      uint8_t *p = (uint8_t *)buf;
      // Copy the GPR registers
      memcpy(p, &m_state.context.gpr, sizeof(GPR));
      p += sizeof(GPR);

      // Walk around the gaps in the FPU regs
      memcpy(p, &m_state.context.fpu.no_avx.__fpu_fcw, 5);
      // We read 5 bytes, but we skip 6 to account for __fpu_rsrv1
      // to match the g_fpu_registers_* tables.
      p += 6;
      memcpy(p, &m_state.context.fpu.no_avx.__fpu_fop, 8);
      p += 8;
      memcpy(p, &m_state.context.fpu.no_avx.__fpu_dp, 6);
      p += 6;
      memcpy(p, &m_state.context.fpu.no_avx.__fpu_mxcsr, 8);
      p += 8;

      // Work around the padding between the stmm registers as they are 16
      // byte structs with 10 bytes of the value in each
      for (size_t i = 0; i < 8; ++i) {
        memcpy(p, &m_state.context.fpu.no_avx.__fpu_stmm0 + i, 10);
        p += 10;
      }

      if(CPUHasAVX512f() || FORCE_AVX_REGS) {
        for (size_t i = 0; i < 8; ++i) {
          memcpy(p, &m_state.context.fpu.avx512f.__fpu_k0 + i, 8);
          p += 8;
        }
      }

      if (CPUHasAVX() || FORCE_AVX_REGS) {
        // Interleave the XMM and YMMH registers to make the YMM registers
        for (size_t i = 0; i < 16; ++i) {
          memcpy(p, &m_state.context.fpu.avx.__fpu_xmm0 + i, 16);
          p += 16;
          memcpy(p, &m_state.context.fpu.avx.__fpu_ymmh0 + i, 16);
          p += 16;
        }
        if(CPUHasAVX512f() || FORCE_AVX_REGS) {
          for (size_t i = 0; i < 16; ++i) {
            memcpy(p, &m_state.context.fpu.avx512f.__fpu_zmmh0 + i, 32);
            p += 32;
          }
          for (size_t i = 0; i < 16; ++i) {
            memcpy(p, &m_state.context.fpu.avx512f.__fpu_zmm16 + i, 64);
            p += 64;
          }
        }
      } else {
        // Copy the XMM registers in a single block
        memcpy(p, &m_state.context.fpu.no_avx.__fpu_xmm0, 16 * 16);
        p += 16 * 16;
      }

      // Copy the exception registers
      memcpy(p, &m_state.context.exc, sizeof(EXC));
      p += sizeof(EXC);

      // make sure we end up with exactly what we think we should have
      size_t bytes_written = p - (uint8_t *)buf;
      UNUSED_IF_ASSERT_DISABLED(bytes_written);
      assert(bytes_written == size);
    }
  }

  DNBLogThreadedIf(
      LOG_THREAD,
      "DNBArchImplX86_64::GetRegisterContext (buf = %p, len = %llu) => %u", buf,
      (uint64_t)buf_len, size);
  // Return the size of the register context even if NULL was passed in
  return size;
}

nub_size_t DNBArchImplX86_64::SetRegisterContext(const void *buf,
                                                 nub_size_t buf_len) {
  uint32_t size = GetRegisterContextSize();
  if (buf == NULL || buf_len == 0)
    size = 0;

  if (size) {
    if (size > buf_len)
      size = static_cast<uint32_t>(buf_len);

    const uint8_t *p = (const uint8_t *)buf;
    // Copy the GPR registers
    memcpy(&m_state.context.gpr, p, sizeof(GPR));
    p += sizeof(GPR);

    // Copy fcw through mxcsrmask as there is no padding
    memcpy(&m_state.context.fpu.no_avx.__fpu_fcw, p, 5);
    // We wrote 5 bytes, but we skip 6 to account for __fpu_rsrv1
    // to match the g_fpu_registers_* tables.
    p += 6;
    memcpy(&m_state.context.fpu.no_avx.__fpu_fop, p, 8);
    p += 8;
    memcpy(&m_state.context.fpu.no_avx.__fpu_dp, p, 6);
    p += 6;
    memcpy(&m_state.context.fpu.no_avx.__fpu_mxcsr, p, 8);
    p += 8;

    // Work around the padding between the stmm registers as they are 16
    // byte structs with 10 bytes of the value in each
    for (size_t i = 0; i < 8; ++i) {
      memcpy(&m_state.context.fpu.no_avx.__fpu_stmm0 + i, p, 10);
      p += 10;
    }

    if(CPUHasAVX512f() || FORCE_AVX_REGS) {
      for (size_t i = 0; i < 8; ++i) {
        memcpy(&m_state.context.fpu.avx512f.__fpu_k0 + i, p, 8);
        p += 8;
      }
    }

    if (CPUHasAVX() || FORCE_AVX_REGS) {
      // Interleave the XMM and YMMH registers to make the YMM registers
      for (size_t i = 0; i < 16; ++i) {
        memcpy(&m_state.context.fpu.avx.__fpu_xmm0 + i, p, 16);
        p += 16;
        memcpy(&m_state.context.fpu.avx.__fpu_ymmh0 + i, p, 16);
        p += 16;
      }
      if(CPUHasAVX512f() || FORCE_AVX_REGS) {
          for (size_t i = 0; i < 16; ++i) {
            memcpy(&m_state.context.fpu.avx512f.__fpu_zmmh0 + i, p, 32);
            p += 32;
          }
          for (size_t i = 0; i < 16; ++i) {
            memcpy(&m_state.context.fpu.avx512f.__fpu_zmm16 + i, p, 64);
            p += 64;
          }
        }
    } else {
      // Copy the XMM registers in a single block
      memcpy(&m_state.context.fpu.no_avx.__fpu_xmm0, p, 16 * 16);
      p += 16 * 16;
    }

    // Copy the exception registers
    memcpy(&m_state.context.exc, p, sizeof(EXC));
    p += sizeof(EXC);

    // make sure we end up with exactly what we think we should have
    size_t bytes_written = p - (const uint8_t *)buf;
    UNUSED_IF_ASSERT_DISABLED(bytes_written);
    assert(bytes_written == size);

    kern_return_t kret;
    if ((kret = SetGPRState()) != KERN_SUCCESS)
      DNBLogThreadedIf(LOG_THREAD, "DNBArchImplX86_64::SetRegisterContext (buf "
                                   "= %p, len = %llu) error: GPR regs failed "
                                   "to write: %u",
                       buf, (uint64_t)buf_len, kret);
    if ((kret = SetFPUState()) != KERN_SUCCESS)
      DNBLogThreadedIf(
          LOG_THREAD, "DNBArchImplX86_64::SetRegisterContext (buf = %p, len = "
                      "%llu) error: %s regs failed to write: %u",
          buf, (uint64_t)buf_len, CPUHasAVX() ? "AVX" : "FPU", kret);
    if ((kret = SetEXCState()) != KERN_SUCCESS)
      DNBLogThreadedIf(LOG_THREAD, "DNBArchImplX86_64::SetRegisterContext (buf "
                                   "= %p, len = %llu) error: EXP regs failed "
                                   "to write: %u",
                       buf, (uint64_t)buf_len, kret);
  }
  DNBLogThreadedIf(
      LOG_THREAD,
      "DNBArchImplX86_64::SetRegisterContext (buf = %p, len = %llu) => %llu",
      buf, (uint64_t)buf_len, (uint64_t)size);
  return size;
}

uint32_t DNBArchImplX86_64::SaveRegisterState() {
  kern_return_t kret = ::thread_abort_safely(m_thread->MachPortNumber());
  DNBLogThreadedIf(
      LOG_THREAD, "thread = 0x%4.4x calling thread_abort_safely (tid) => %u "
                  "(SetGPRState() for stop_count = %u)",
      m_thread->MachPortNumber(), kret, m_thread->Process()->StopCount());

  // Always re-read the registers because above we call thread_abort_safely();
  bool force = true;

  if ((kret = GetGPRState(force)) != KERN_SUCCESS) {
    DNBLogThreadedIf(LOG_THREAD, "DNBArchImplX86_64::SaveRegisterState () "
                                 "error: GPR regs failed to read: %u ",
                     kret);
  } else if ((kret = GetFPUState(force)) != KERN_SUCCESS) {
    DNBLogThreadedIf(LOG_THREAD, "DNBArchImplX86_64::SaveRegisterState () "
                                 "error: %s regs failed to read: %u",
                     CPUHasAVX() ? "AVX" : "FPU", kret);
  } else {
    const uint32_t save_id = GetNextRegisterStateSaveID();
    m_saved_register_states[save_id] = m_state.context;
    return save_id;
  }
  return 0;
}
bool DNBArchImplX86_64::RestoreRegisterState(uint32_t save_id) {
  SaveRegisterStates::iterator pos = m_saved_register_states.find(save_id);
  if (pos != m_saved_register_states.end()) {
    m_state.context.gpr = pos->second.gpr;
    m_state.context.fpu = pos->second.fpu;
    m_state.SetError(e_regSetGPR, Read, 0);
    m_state.SetError(e_regSetFPU, Read, 0);
    kern_return_t kret;
    bool success = true;
    if ((kret = SetGPRState()) != KERN_SUCCESS) {
      DNBLogThreadedIf(LOG_THREAD, "DNBArchImplX86_64::RestoreRegisterState "
                                   "(save_id = %u) error: GPR regs failed to "
                                   "write: %u",
                       save_id, kret);
      success = false;
    } else if ((kret = SetFPUState()) != KERN_SUCCESS) {
      DNBLogThreadedIf(LOG_THREAD, "DNBArchImplX86_64::RestoreRegisterState "
                                   "(save_id = %u) error: %s regs failed to "
                                   "write: %u",
                       save_id, CPUHasAVX() ? "AVX" : "FPU", kret);
      success = false;
    }
    m_saved_register_states.erase(pos);
    return success;
  }
  return false;
}

kern_return_t DNBArchImplX86_64::GetRegisterState(int set, bool force) {
  switch (set) {
  case e_regSetALL:
    return GetGPRState(force) | GetFPUState(force) | GetEXCState(force);
  case e_regSetGPR:
    return GetGPRState(force);
  case e_regSetFPU:
    return GetFPUState(force);
  case e_regSetEXC:
    return GetEXCState(force);
  default:
    break;
  }
  return KERN_INVALID_ARGUMENT;
}

kern_return_t DNBArchImplX86_64::SetRegisterState(int set) {
  // Make sure we have a valid context to set.
  if (RegisterSetStateIsValid(set)) {
    switch (set) {
    case e_regSetALL:
      return SetGPRState() | SetFPUState() | SetEXCState();
    case e_regSetGPR:
      return SetGPRState();
    case e_regSetFPU:
      return SetFPUState();
    case e_regSetEXC:
      return SetEXCState();
    default:
      break;
    }
  }
  return KERN_INVALID_ARGUMENT;
}

bool DNBArchImplX86_64::RegisterSetStateIsValid(int set) const {
  return m_state.RegsAreValid(set);
}

#endif // #if defined (__i386__) || defined (__x86_64__)
