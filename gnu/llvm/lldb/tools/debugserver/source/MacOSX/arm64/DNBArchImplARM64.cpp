//===-- DNBArchImplARM64.cpp ------------------------------------*- C++ -*-===//
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

#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)

#include "MacOSX/arm64/DNBArchImplARM64.h"

#if defined(ARM_THREAD_STATE64_COUNT)

#include "DNB.h"
#include "DNBBreakpoint.h"
#include "DNBLog.h"
#include "DNBRegisterInfo.h"
#include "MacOSX/MachProcess.h"
#include "MacOSX/MachThread.h"

#include <cinttypes>
#include <sys/sysctl.h>

#undef DEBUGSERVER_IS_ARM64E
#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#if defined(__LP64__)
#define DEBUGSERVER_IS_ARM64E 1
#endif
#endif

// Break only in privileged or user mode
// (PAC bits in the DBGWVRn_EL1 watchpoint control register)
#define S_USER ((uint32_t)(2u << 1))

#define BCR_ENABLE ((uint32_t)(1u))
#define WCR_ENABLE ((uint32_t)(1u))

// Watchpoint load/store
// (LSC bits in the DBGWVRn_EL1 watchpoint control register)
#define WCR_LOAD ((uint32_t)(1u << 3))
#define WCR_STORE ((uint32_t)(1u << 4))

// Single instruction step
// (SS bit in the MDSCR_EL1 register)
#define SS_ENABLE ((uint32_t)(1u))

static const uint8_t g_arm64_breakpoint_opcode[] = {
    0x00, 0x00, 0x20, 0xD4}; // "brk #0", 0xd4200000 in BE byte order

// If we need to set one logical watchpoint by using
// two hardware watchpoint registers, the watchpoint
// will be split into a "high" and "low" watchpoint.
// Record both of them in the LoHi array.

// It's safe to initialize to all 0's since
// hi > lo and therefore LoHi[i] cannot be 0.
static uint32_t LoHi[16] = {0};

void DNBArchMachARM64::Initialize() {
  DNBArchPluginInfo arch_plugin_info = {
      CPU_TYPE_ARM64, DNBArchMachARM64::Create,
      DNBArchMachARM64::GetRegisterSetInfo,
      DNBArchMachARM64::SoftwareBreakpointOpcode};

  // Register this arch plug-in with the main protocol class
  DNBArchProtocol::RegisterArchPlugin(arch_plugin_info);

  DNBArchPluginInfo arch_plugin_info_32 = {
      CPU_TYPE_ARM64_32, DNBArchMachARM64::Create,
      DNBArchMachARM64::GetRegisterSetInfo,
      DNBArchMachARM64::SoftwareBreakpointOpcode};

  // Register this arch plug-in with the main protocol class
  DNBArchProtocol::RegisterArchPlugin(arch_plugin_info_32);
}

DNBArchProtocol *DNBArchMachARM64::Create(MachThread *thread) {
  DNBArchMachARM64 *obj = new DNBArchMachARM64(thread);

  return obj;
}

const uint8_t *
DNBArchMachARM64::SoftwareBreakpointOpcode(nub_size_t byte_size) {
  return g_arm64_breakpoint_opcode;
}

uint32_t DNBArchMachARM64::GetCPUType() { return CPU_TYPE_ARM64; }

static uint64_t clear_pac_bits(uint64_t value) {
  uint32_t addressing_bits = 0;
  if (!DNBGetAddressingBits(addressing_bits))
    return value;

    // On arm64_32, no ptrauth bits to clear
#if !defined(__LP64__)
  return value;
#endif

  uint64_t mask = ((1ULL << addressing_bits) - 1);

  // Normally PAC bit clearing needs to check b55 and either set the
  // non-addressing bits, or clear them.  But the register values we
  // get from thread_get_state on an arm64e process don't follow this
  // convention?, at least when there's been a PAC auth failure in
  // the inferior.
  // Userland processes are always in low memory, so this
  // hardcoding b55 == 0 PAC stripping behavior here.

  return value & mask; // high bits cleared to 0
}

uint64_t DNBArchMachARM64::GetPC(uint64_t failValue) {
  // Get program counter
  if (GetGPRState(false) == KERN_SUCCESS)
#if defined(DEBUGSERVER_IS_ARM64E)
    return clear_pac_bits(
        reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_pc));
#else
    return m_state.context.gpr.__pc;
#endif
  return failValue;
}

kern_return_t DNBArchMachARM64::SetPC(uint64_t value) {
  // Get program counter
  kern_return_t err = GetGPRState(false);
  if (err == KERN_SUCCESS) {
#if defined(__LP64__)
#if __has_feature(ptrauth_calls)
    // The incoming value could be garbage.  Strip it to avoid
    // trapping when it gets resigned in the thread state.
    value = (uint64_t) ptrauth_strip((void*) value, ptrauth_key_function_pointer);
    value = (uint64_t) ptrauth_sign_unauthenticated((void*) value, ptrauth_key_function_pointer, 0);
#endif
    arm_thread_state64_set_pc_fptr (m_state.context.gpr, (void*) value);
#else
    m_state.context.gpr.__pc = value;
#endif
    err = SetGPRState();
  }
  return err == KERN_SUCCESS;
}

uint64_t DNBArchMachARM64::GetSP(uint64_t failValue) {
  // Get stack pointer
  if (GetGPRState(false) == KERN_SUCCESS)
#if defined(DEBUGSERVER_IS_ARM64E)
    return clear_pac_bits(
        reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_sp));
#else
    return m_state.context.gpr.__sp;
#endif
  return failValue;
}

kern_return_t DNBArchMachARM64::GetGPRState(bool force) {
  int set = e_regSetGPR;
  // Check if we have valid cached registers
  if (!force && m_state.GetError(set, Read) == KERN_SUCCESS)
    return KERN_SUCCESS;

  // Read the registers from our thread
  mach_msg_type_number_t count = e_regSetGPRCount;
  kern_return_t kret =
      ::thread_get_state(m_thread->MachPortNumber(), ARM_THREAD_STATE64,
                         (thread_state_t)&m_state.context.gpr, &count);
  if (DNBLogEnabledForAny(LOG_THREAD)) {
    uint64_t *x = &m_state.context.gpr.__x[0];

    const char *log_str = "thread_get_state signed regs "
                          "\n   fp=%16.16llx"
                          "\n   lr=%16.16llx"
                          "\n   sp=%16.16llx"
                          "\n   pc=%16.16llx";
#if defined(DEBUGSERVER_IS_ARM64E)
    DNBLogThreaded(log_str,
                   reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_fp),
                   reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_lr),
                   reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_sp),
                   reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_pc));
#else
    DNBLogThreaded(log_str, m_state.context.gpr.__fp, m_state.context.gpr.__lr,
                   m_state.context.gpr.__sp, m_state.context.gpr.__pc);
#endif

#if defined(DEBUGSERVER_IS_ARM64E)
    uint64_t log_fp = clear_pac_bits(
        reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_fp));
    uint64_t log_lr = clear_pac_bits(
        reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_lr));
    uint64_t log_sp = clear_pac_bits(
        reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_sp));
    uint64_t log_pc = clear_pac_bits(
        reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_pc));
#else
    uint64_t log_fp = m_state.context.gpr.__fp;
    uint64_t log_lr = m_state.context.gpr.__lr;
    uint64_t log_sp = m_state.context.gpr.__sp;
    uint64_t log_pc = m_state.context.gpr.__pc;
#endif
    DNBLogThreaded(
        "thread_get_state(0x%4.4x, %u, &gpr, %u) => 0x%8.8x (count = %u) regs"
        "\n   x0=%16.16llx"
        "\n   x1=%16.16llx"
        "\n   x2=%16.16llx"
        "\n   x3=%16.16llx"
        "\n   x4=%16.16llx"
        "\n   x5=%16.16llx"
        "\n   x6=%16.16llx"
        "\n   x7=%16.16llx"
        "\n   x8=%16.16llx"
        "\n   x9=%16.16llx"
        "\n  x10=%16.16llx"
        "\n  x11=%16.16llx"
        "\n  x12=%16.16llx"
        "\n  x13=%16.16llx"
        "\n  x14=%16.16llx"
        "\n  x15=%16.16llx"
        "\n  x16=%16.16llx"
        "\n  x17=%16.16llx"
        "\n  x18=%16.16llx"
        "\n  x19=%16.16llx"
        "\n  x20=%16.16llx"
        "\n  x21=%16.16llx"
        "\n  x22=%16.16llx"
        "\n  x23=%16.16llx"
        "\n  x24=%16.16llx"
        "\n  x25=%16.16llx"
        "\n  x26=%16.16llx"
        "\n  x27=%16.16llx"
        "\n  x28=%16.16llx"
        "\n   fp=%16.16llx"
        "\n   lr=%16.16llx"
        "\n   sp=%16.16llx"
        "\n   pc=%16.16llx"
        "\n cpsr=%8.8x",
        m_thread->MachPortNumber(), e_regSetGPR, e_regSetGPRCount, kret, count,
        x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9], x[0], x[11],
        x[12], x[13], x[14], x[15], x[16], x[17], x[18], x[19], x[20], x[21],
        x[22], x[23], x[24], x[25], x[26], x[27], x[28],
        log_fp, log_lr, log_sp, log_pc, m_state.context.gpr.__cpsr);
  }
  m_state.SetError(set, Read, kret);
  return kret;
}

kern_return_t DNBArchMachARM64::GetVFPState(bool force) {
  int set = e_regSetVFP;
  // Check if we have valid cached registers
  if (!force && m_state.GetError(set, Read) == KERN_SUCCESS)
    return KERN_SUCCESS;

  // Read the registers from our thread
  mach_msg_type_number_t count = e_regSetVFPCount;
  kern_return_t kret =
      ::thread_get_state(m_thread->MachPortNumber(), ARM_NEON_STATE64,
                         (thread_state_t)&m_state.context.vfp, &count);
  if (DNBLogEnabledForAny(LOG_THREAD)) {
#if defined(__arm64__) || defined(__aarch64__)
    DNBLogThreaded(
        "thread_get_state(0x%4.4x, %u, &vfp, %u) => 0x%8.8x (count = %u) regs"
        "\n   q0  = 0x%16.16llx%16.16llx"
        "\n   q1  = 0x%16.16llx%16.16llx"
        "\n   q2  = 0x%16.16llx%16.16llx"
        "\n   q3  = 0x%16.16llx%16.16llx"
        "\n   q4  = 0x%16.16llx%16.16llx"
        "\n   q5  = 0x%16.16llx%16.16llx"
        "\n   q6  = 0x%16.16llx%16.16llx"
        "\n   q7  = 0x%16.16llx%16.16llx"
        "\n   q8  = 0x%16.16llx%16.16llx"
        "\n   q9  = 0x%16.16llx%16.16llx"
        "\n   q10 = 0x%16.16llx%16.16llx"
        "\n   q11 = 0x%16.16llx%16.16llx"
        "\n   q12 = 0x%16.16llx%16.16llx"
        "\n   q13 = 0x%16.16llx%16.16llx"
        "\n   q14 = 0x%16.16llx%16.16llx"
        "\n   q15 = 0x%16.16llx%16.16llx"
        "\n   q16 = 0x%16.16llx%16.16llx"
        "\n   q17 = 0x%16.16llx%16.16llx"
        "\n   q18 = 0x%16.16llx%16.16llx"
        "\n   q19 = 0x%16.16llx%16.16llx"
        "\n   q20 = 0x%16.16llx%16.16llx"
        "\n   q21 = 0x%16.16llx%16.16llx"
        "\n   q22 = 0x%16.16llx%16.16llx"
        "\n   q23 = 0x%16.16llx%16.16llx"
        "\n   q24 = 0x%16.16llx%16.16llx"
        "\n   q25 = 0x%16.16llx%16.16llx"
        "\n   q26 = 0x%16.16llx%16.16llx"
        "\n   q27 = 0x%16.16llx%16.16llx"
        "\n   q28 = 0x%16.16llx%16.16llx"
        "\n   q29 = 0x%16.16llx%16.16llx"
        "\n   q30 = 0x%16.16llx%16.16llx"
        "\n   q31 = 0x%16.16llx%16.16llx"
        "\n  fpsr = 0x%8.8x"
        "\n  fpcr = 0x%8.8x\n\n",
        m_thread->MachPortNumber(), e_regSetVFP, e_regSetVFPCount, kret, count,
        ((uint64_t *)&m_state.context.vfp.__v[0])[0],
        ((uint64_t *)&m_state.context.vfp.__v[0])[1],
        ((uint64_t *)&m_state.context.vfp.__v[1])[0],
        ((uint64_t *)&m_state.context.vfp.__v[1])[1],
        ((uint64_t *)&m_state.context.vfp.__v[2])[0],
        ((uint64_t *)&m_state.context.vfp.__v[2])[1],
        ((uint64_t *)&m_state.context.vfp.__v[3])[0],
        ((uint64_t *)&m_state.context.vfp.__v[3])[1],
        ((uint64_t *)&m_state.context.vfp.__v[4])[0],
        ((uint64_t *)&m_state.context.vfp.__v[4])[1],
        ((uint64_t *)&m_state.context.vfp.__v[5])[0],
        ((uint64_t *)&m_state.context.vfp.__v[5])[1],
        ((uint64_t *)&m_state.context.vfp.__v[6])[0],
        ((uint64_t *)&m_state.context.vfp.__v[6])[1],
        ((uint64_t *)&m_state.context.vfp.__v[7])[0],
        ((uint64_t *)&m_state.context.vfp.__v[7])[1],
        ((uint64_t *)&m_state.context.vfp.__v[8])[0],
        ((uint64_t *)&m_state.context.vfp.__v[8])[1],
        ((uint64_t *)&m_state.context.vfp.__v[9])[0],
        ((uint64_t *)&m_state.context.vfp.__v[9])[1],
        ((uint64_t *)&m_state.context.vfp.__v[10])[0],
        ((uint64_t *)&m_state.context.vfp.__v[10])[1],
        ((uint64_t *)&m_state.context.vfp.__v[11])[0],
        ((uint64_t *)&m_state.context.vfp.__v[11])[1],
        ((uint64_t *)&m_state.context.vfp.__v[12])[0],
        ((uint64_t *)&m_state.context.vfp.__v[12])[1],
        ((uint64_t *)&m_state.context.vfp.__v[13])[0],
        ((uint64_t *)&m_state.context.vfp.__v[13])[1],
        ((uint64_t *)&m_state.context.vfp.__v[14])[0],
        ((uint64_t *)&m_state.context.vfp.__v[14])[1],
        ((uint64_t *)&m_state.context.vfp.__v[15])[0],
        ((uint64_t *)&m_state.context.vfp.__v[15])[1],
        ((uint64_t *)&m_state.context.vfp.__v[16])[0],
        ((uint64_t *)&m_state.context.vfp.__v[16])[1],
        ((uint64_t *)&m_state.context.vfp.__v[17])[0],
        ((uint64_t *)&m_state.context.vfp.__v[17])[1],
        ((uint64_t *)&m_state.context.vfp.__v[18])[0],
        ((uint64_t *)&m_state.context.vfp.__v[18])[1],
        ((uint64_t *)&m_state.context.vfp.__v[19])[0],
        ((uint64_t *)&m_state.context.vfp.__v[19])[1],
        ((uint64_t *)&m_state.context.vfp.__v[20])[0],
        ((uint64_t *)&m_state.context.vfp.__v[20])[1],
        ((uint64_t *)&m_state.context.vfp.__v[21])[0],
        ((uint64_t *)&m_state.context.vfp.__v[21])[1],
        ((uint64_t *)&m_state.context.vfp.__v[22])[0],
        ((uint64_t *)&m_state.context.vfp.__v[22])[1],
        ((uint64_t *)&m_state.context.vfp.__v[23])[0],
        ((uint64_t *)&m_state.context.vfp.__v[23])[1],
        ((uint64_t *)&m_state.context.vfp.__v[24])[0],
        ((uint64_t *)&m_state.context.vfp.__v[24])[1],
        ((uint64_t *)&m_state.context.vfp.__v[25])[0],
        ((uint64_t *)&m_state.context.vfp.__v[25])[1],
        ((uint64_t *)&m_state.context.vfp.__v[26])[0],
        ((uint64_t *)&m_state.context.vfp.__v[26])[1],
        ((uint64_t *)&m_state.context.vfp.__v[27])[0],
        ((uint64_t *)&m_state.context.vfp.__v[27])[1],
        ((uint64_t *)&m_state.context.vfp.__v[28])[0],
        ((uint64_t *)&m_state.context.vfp.__v[28])[1],
        ((uint64_t *)&m_state.context.vfp.__v[29])[0],
        ((uint64_t *)&m_state.context.vfp.__v[29])[1],
        ((uint64_t *)&m_state.context.vfp.__v[30])[0],
        ((uint64_t *)&m_state.context.vfp.__v[30])[1],
        ((uint64_t *)&m_state.context.vfp.__v[31])[0],
        ((uint64_t *)&m_state.context.vfp.__v[31])[1],
        m_state.context.vfp.__fpsr, m_state.context.vfp.__fpcr);
#endif
  }
  m_state.SetError(set, Read, kret);
  return kret;
}

kern_return_t DNBArchMachARM64::GetEXCState(bool force) {
  int set = e_regSetEXC;
  // Check if we have valid cached registers
  if (!force && m_state.GetError(set, Read) == KERN_SUCCESS)
    return KERN_SUCCESS;

  // Read the registers from our thread
  mach_msg_type_number_t count = e_regSetEXCCount;
  kern_return_t kret =
      ::thread_get_state(m_thread->MachPortNumber(), ARM_EXCEPTION_STATE64,
                         (thread_state_t)&m_state.context.exc, &count);
  m_state.SetError(set, Read, kret);
  return kret;
}

#if 0
static void DumpDBGState(const arm_debug_state_t &dbg) {
  uint32_t i = 0;
  for (i = 0; i < 16; i++)
    DNBLogThreadedIf(LOG_STEP, "BVR%-2u/BCR%-2u = { 0x%8.8x, 0x%8.8x } "
                               "WVR%-2u/WCR%-2u = { 0x%8.8x, 0x%8.8x }",
                     i, i, dbg.__bvr[i], dbg.__bcr[i], i, i, dbg.__wvr[i],
                     dbg.__wcr[i]);
}
#endif

kern_return_t DNBArchMachARM64::GetDBGState(bool force) {
  int set = e_regSetDBG;

  // Check if we have valid cached registers
  if (!force && m_state.GetError(set, Read) == KERN_SUCCESS)
    return KERN_SUCCESS;

  // Read the registers from our thread
  mach_msg_type_number_t count = e_regSetDBGCount;
  kern_return_t kret =
      ::thread_get_state(m_thread->MachPortNumber(), ARM_DEBUG_STATE64,
                         (thread_state_t)&m_state.dbg, &count);
  m_state.SetError(set, Read, kret);

  return kret;
}

kern_return_t DNBArchMachARM64::SetGPRState() {
  int set = e_regSetGPR;
  kern_return_t kret = ::thread_set_state(
      m_thread->MachPortNumber(), ARM_THREAD_STATE64,
      (thread_state_t)&m_state.context.gpr, e_regSetGPRCount);
  m_state.SetError(set, Write,
                   kret); // Set the current write error for this register set
  m_state.InvalidateRegisterSetState(set); // Invalidate the current register
                                           // state in case registers are read
                                           // back differently
  return kret;                             // Return the error code
}

kern_return_t DNBArchMachARM64::SetVFPState() {
  int set = e_regSetVFP;
  kern_return_t kret = ::thread_set_state(
      m_thread->MachPortNumber(), ARM_NEON_STATE64,
      (thread_state_t)&m_state.context.vfp, e_regSetVFPCount);
  m_state.SetError(set, Write,
                   kret); // Set the current write error for this register set
  m_state.InvalidateRegisterSetState(set); // Invalidate the current register
                                           // state in case registers are read
                                           // back differently
  return kret;                             // Return the error code
}

kern_return_t DNBArchMachARM64::SetEXCState() {
  int set = e_regSetEXC;
  kern_return_t kret = ::thread_set_state(
      m_thread->MachPortNumber(), ARM_EXCEPTION_STATE64,
      (thread_state_t)&m_state.context.exc, e_regSetEXCCount);
  m_state.SetError(set, Write,
                   kret); // Set the current write error for this register set
  m_state.InvalidateRegisterSetState(set); // Invalidate the current register
                                           // state in case registers are read
                                           // back differently
  return kret;                             // Return the error code
}

kern_return_t DNBArchMachARM64::SetDBGState(bool also_set_on_task) {
  int set = e_regSetDBG;
  kern_return_t kret =
      ::thread_set_state(m_thread->MachPortNumber(), ARM_DEBUG_STATE64,
                         (thread_state_t)&m_state.dbg, e_regSetDBGCount);
  if (also_set_on_task) {
    kern_return_t task_kret = task_set_state(
        m_thread->Process()->Task().TaskPort(), ARM_DEBUG_STATE64,
        (thread_state_t)&m_state.dbg, e_regSetDBGCount);
    if (task_kret != KERN_SUCCESS)
      DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::SetDBGState failed "
                                        "to set debug control register state: "
                                        "0x%8.8x.",
                       task_kret);
  }
  m_state.SetError(set, Write,
                   kret); // Set the current write error for this register set
  m_state.InvalidateRegisterSetState(set); // Invalidate the current register
                                           // state in case registers are read
                                           // back differently

  return kret; // Return the error code
}

void DNBArchMachARM64::ThreadWillResume() {
  // Do we need to step this thread? If so, let the mach thread tell us so.
  if (m_thread->IsStepping()) {
    EnableHardwareSingleStep(true);
  }

  // Disable the triggered watchpoint temporarily before we resume.
  // Plus, we try to enable hardware single step to execute past the instruction
  // which triggered our watchpoint.
  if (m_watchpoint_did_occur) {
    if (m_watchpoint_hw_index >= 0) {
      kern_return_t kret = GetDBGState(false);
      if (kret == KERN_SUCCESS &&
          !IsWatchpointEnabled(m_state.dbg, m_watchpoint_hw_index)) {
        // The watchpoint might have been disabled by the user.  We don't need
        // to do anything at all
        // to enable hardware single stepping.
        m_watchpoint_did_occur = false;
        m_watchpoint_hw_index = -1;
        return;
      }

      DisableHardwareWatchpoint(m_watchpoint_hw_index, false);
      DNBLogThreadedIf(LOG_WATCHPOINTS,
                       "DNBArchMachARM64::ThreadWillResume() "
                       "DisableHardwareWatchpoint(%d) called",
                       m_watchpoint_hw_index);

      // Enable hardware single step to move past the watchpoint-triggering
      // instruction.
      m_watchpoint_resume_single_step_enabled =
          (EnableHardwareSingleStep(true) == KERN_SUCCESS);

      // If we are not able to enable single step to move past the
      // watchpoint-triggering instruction,
      // at least we should reset the two watchpoint member variables so that
      // the next time around
      // this callback function is invoked, the enclosing logical branch is
      // skipped.
      if (!m_watchpoint_resume_single_step_enabled) {
        // Reset the two watchpoint member variables.
        m_watchpoint_did_occur = false;
        m_watchpoint_hw_index = -1;
        DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::ThreadWillResume()"
                                          " failed to enable single step");
      } else
        DNBLogThreadedIf(LOG_WATCHPOINTS,
                         "DNBArchMachARM64::ThreadWillResume() "
                         "succeeded to enable single step");
    }
  }
}

bool DNBArchMachARM64::NotifyException(MachException::Data &exc) {

  switch (exc.exc_type) {
  default:
    break;
  case EXC_BREAKPOINT:
    if (exc.exc_data.size() == 2 && exc.exc_data[0] == EXC_ARM_DA_DEBUG) {
      // The data break address is passed as exc_data[1].
      nub_addr_t addr = exc.exc_data[1];
      // Find the hardware index with the side effect of possibly massaging the
      // addr to return the starting address as seen from the debugger side.
      uint32_t hw_index = GetHardwareWatchpointHit(addr);

      // One logical watchpoint was split into two watchpoint locations because
      // it was too big.  If the watchpoint exception is indicating the 2nd half
      // of the two-parter, find the address of the 1st half and report that --
      // that's what lldb is going to expect to see.
      DNBLogThreadedIf(LOG_WATCHPOINTS,
                       "DNBArchMachARM64::NotifyException "
                       "watchpoint %d was hit on address "
                       "0x%llx",
                       hw_index, (uint64_t)addr);
      const uint32_t num_watchpoints = NumSupportedHardwareWatchpoints();
      for (uint32_t i = 0; i < num_watchpoints; i++) {
        if (LoHi[i] != 0 && LoHi[i] == hw_index && LoHi[i] != i &&
            GetWatchpointAddressByIndex(i) != INVALID_NUB_ADDRESS) {
          addr = GetWatchpointAddressByIndex(i);
          DNBLogThreadedIf(LOG_WATCHPOINTS,
                           "DNBArchMachARM64::NotifyException "
                           "It is a linked watchpoint; "
                           "rewritten to index %d addr 0x%llx",
                           LoHi[i], (uint64_t)addr);
        }
      }

      if (hw_index != INVALID_NUB_HW_INDEX) {
        m_watchpoint_did_occur = true;
        m_watchpoint_hw_index = hw_index;
        exc.exc_data[1] = addr;
        // Piggyback the hw_index in the exc.data.
        exc.exc_data.push_back(hw_index);
      }

      return true;
    }
    // detect a __builtin_debugtrap instruction pattern ("brk #0xf000")
    // and advance the $pc past it, so that the user can continue execution.
    // Generally speaking, this knowledge should be centralized in lldb,
    // recognizing the builtin_trap instruction and knowing how to advance
    // the pc past it, so that continue etc work.
    if (exc.exc_data.size() == 2 && exc.exc_data[0] == EXC_ARM_BREAKPOINT) {
      nub_addr_t pc = GetPC(INVALID_NUB_ADDRESS);
      if (pc != INVALID_NUB_ADDRESS && pc > 0) {
        DNBBreakpoint *bp =
            m_thread->Process()->Breakpoints().FindByAddress(pc);
        if (bp == nullptr) {
          uint8_t insnbuf[4];
          if (m_thread->Process()->ReadMemory(pc, 4, insnbuf) == 4) {
            uint8_t builtin_debugtrap_insn[4] = {0x00, 0x00, 0x3e,
                                                 0xd4}; // brk #0xf000
            if (memcmp(insnbuf, builtin_debugtrap_insn, 4) == 0) {
              SetPC(pc + 4);
            }
          }
        }
      }
    }
    break;
  }
  return false;
}

bool DNBArchMachARM64::ThreadDidStop() {
  bool success = true;

  m_state.InvalidateAllRegisterStates();

  if (m_watchpoint_resume_single_step_enabled) {
    // Great!  We now disable the hardware single step as well as re-enable the
    // hardware watchpoint.
    // See also ThreadWillResume().
    if (EnableHardwareSingleStep(false) == KERN_SUCCESS) {
      if (m_watchpoint_did_occur && m_watchpoint_hw_index >= 0) {
        ReenableHardwareWatchpoint(m_watchpoint_hw_index);
        m_watchpoint_resume_single_step_enabled = false;
        m_watchpoint_did_occur = false;
        m_watchpoint_hw_index = -1;
      } else {
        DNBLogError("internal error detected: m_watchpoint_resume_step_enabled "
                    "is true but (m_watchpoint_did_occur && "
                    "m_watchpoint_hw_index >= 0) does not hold!");
      }
    } else {
      DNBLogError("internal error detected: m_watchpoint_resume_step_enabled "
                  "is true but unable to disable single step!");
    }
  }

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

// Set the single step bit in the processor status register.
kern_return_t DNBArchMachARM64::EnableHardwareSingleStep(bool enable) {
  DNBError err;
  DNBLogThreadedIf(LOG_STEP, "%s( enable = %d )", __FUNCTION__, enable);

  err = GetGPRState(false);

  if (err.Fail()) {
    err.LogThreaded("%s: failed to read the GPR registers", __FUNCTION__);
    return err.Status();
  }

  err = GetDBGState(false);

  if (err.Fail()) {
    err.LogThreaded("%s: failed to read the DBG registers", __FUNCTION__);
    return err.Status();
  }

#if defined(DEBUGSERVER_IS_ARM64E)
  uint64_t pc = clear_pac_bits(
      reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_pc));
#else
  uint64_t pc = m_state.context.gpr.__pc;
#endif

  if (enable) {
    DNBLogThreadedIf(LOG_STEP,
                     "%s: Setting MDSCR_EL1 Single Step bit at pc 0x%llx",
                     __FUNCTION__, pc);
    m_state.dbg.__mdscr_el1 |= SS_ENABLE;
  } else {
    DNBLogThreadedIf(LOG_STEP,
                     "%s: Clearing MDSCR_EL1 Single Step bit at pc 0x%llx",
                     __FUNCTION__, pc);
    m_state.dbg.__mdscr_el1 &= ~(SS_ENABLE);
  }

  return SetDBGState(false);
}

// return 1 if bit "BIT" is set in "value"
static inline uint32_t bit(uint32_t value, uint32_t bit) {
  return (value >> bit) & 1u;
}

// return the bitfield "value[msbit:lsbit]".
static inline uint64_t bits(uint64_t value, uint32_t msbit, uint32_t lsbit) {
  assert(msbit >= lsbit);
  uint64_t shift_left = sizeof(value) * 8 - 1 - msbit;
  value <<=
      shift_left; // shift anything above the msbit off of the unsigned edge
  value >>= shift_left + lsbit; // shift it back again down to the lsbit
                                // (including undoing any shift from above)
  return value;                 // return our result
}

uint32_t DNBArchMachARM64::NumSupportedHardwareWatchpoints() {
  // Set the init value to something that will let us know that we need to
  // autodetect how many watchpoints are supported dynamically...
  static uint32_t g_num_supported_hw_watchpoints = UINT_MAX;
  if (g_num_supported_hw_watchpoints == UINT_MAX) {
    // Set this to zero in case we can't tell if there are any HW breakpoints
    g_num_supported_hw_watchpoints = 0;

    size_t len;
    uint32_t n = 0;
    len = sizeof(n);
    if (::sysctlbyname("hw.optional.watchpoint", &n, &len, NULL, 0) == 0) {
      g_num_supported_hw_watchpoints = n;
      DNBLogThreadedIf(LOG_THREAD, "hw.optional.watchpoint=%u", n);
    } else {
// For AArch64 we would need to look at ID_AA64DFR0_EL1 but debugserver runs in
// EL0 so it can't
// access that reg.  The kernel should have filled in the sysctls based on it
// though.
#if defined(__arm__)
      uint32_t register_DBGDIDR;

      asm("mrc p14, 0, %0, c0, c0, 0" : "=r"(register_DBGDIDR));
      uint32_t numWRPs = bits(register_DBGDIDR, 31, 28);
      // Zero is reserved for the WRP count, so don't increment it if it is zero
      if (numWRPs > 0)
        numWRPs++;
      g_num_supported_hw_watchpoints = numWRPs;
      DNBLogThreadedIf(LOG_THREAD,
                       "Number of supported hw watchpoints via asm():  %d",
                       g_num_supported_hw_watchpoints);
#endif
    }
  }
  return g_num_supported_hw_watchpoints;
}

uint32_t DNBArchMachARM64::NumSupportedHardwareBreakpoints() {
  // Set the init value to something that will let us know that we need to
  // autodetect how many breakpoints are supported dynamically...
  static uint32_t g_num_supported_hw_breakpoints = UINT_MAX;
  if (g_num_supported_hw_breakpoints == UINT_MAX) {
    // Set this to zero in case we can't tell if there are any HW breakpoints
    g_num_supported_hw_breakpoints = 0;

    size_t len;
    uint32_t n = 0;
    len = sizeof(n);
    if (::sysctlbyname("hw.optional.breakpoint", &n, &len, NULL, 0) == 0) {
      g_num_supported_hw_breakpoints = n;
      DNBLogThreadedIf(LOG_THREAD, "hw.optional.breakpoint=%u", n);
    } else {
// For AArch64 we would need to look at ID_AA64DFR0_EL1 but debugserver runs in
// EL0 so it can't access that reg.  The kernel should have filled in the
// sysctls based on it though.
#if defined(__arm__)
      uint32_t register_DBGDIDR;

      asm("mrc p14, 0, %0, c0, c0, 0" : "=r"(register_DBGDIDR));
      uint32_t numWRPs = bits(register_DBGDIDR, 31, 28);
      // Zero is reserved for the WRP count, so don't increment it if it is zero
      if (numWRPs > 0)
        numWRPs++;
      g_num_supported_hw_breakpoints = numWRPs;
      DNBLogThreadedIf(LOG_THREAD,
                       "Number of supported hw breakpoint via asm():  %d",
                       g_num_supported_hw_breakpoints);
#endif
    }
  }
  return g_num_supported_hw_breakpoints;
}

uint32_t DNBArchMachARM64::EnableHardwareBreakpoint(nub_addr_t addr,
                                                    nub_size_t size,
                                                    bool also_set_on_task) {
  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::EnableHardwareBreakpoint(addr = "
                   "0x%8.8llx, size = %zu)",
                   (uint64_t)addr, size);

  const uint32_t num_hw_breakpoints = NumSupportedHardwareBreakpoints();

  nub_addr_t aligned_bp_address = addr;
  uint32_t control_value = 0;

  switch (size) {
  case 2:
    control_value = (0x3 << 5) | 7;
    aligned_bp_address &= ~1;
    break;
  case 4:
    control_value = (0xfu << 5) | 7;
    aligned_bp_address &= ~3;
    break;
  };

  // Read the debug state
  kern_return_t kret = GetDBGState(false);
  if (kret == KERN_SUCCESS) {
    // Check to make sure we have the needed hardware support
    uint32_t i = 0;

    for (i = 0; i < num_hw_breakpoints; ++i) {
      if ((m_state.dbg.__bcr[i] & BCR_ENABLE) == 0)
        break; // We found an available hw breakpoint slot (in i)
    }

    // See if we found an available hw breakpoint slot above
    if (i < num_hw_breakpoints) {
      m_state.dbg.__bvr[i] = aligned_bp_address;
      m_state.dbg.__bcr[i] = control_value;

      DNBLogThreadedIf(LOG_WATCHPOINTS,
                       "DNBArchMachARM64::EnableHardwareBreakpoint() "
                       "adding breakpoint on address 0x%llx with control "
                       "register value 0x%x",
                       (uint64_t)m_state.dbg.__bvr[i],
                       (uint32_t)m_state.dbg.__bcr[i]);

      kret = SetDBGState(also_set_on_task);

      DNBLogThreadedIf(LOG_WATCHPOINTS,
                       "DNBArchMachARM64::"
                       "EnableHardwareBreakpoint() "
                       "SetDBGState() => 0x%8.8x.",
                       kret);

      if (kret == KERN_SUCCESS)
        return i;
    } else {
      DNBLogThreadedIf(LOG_WATCHPOINTS,
                       "DNBArchMachARM64::"
                       "EnableHardwareBreakpoint(): All "
                       "hardware resources (%u) are in use.",
                       num_hw_breakpoints);
    }
  }
  return INVALID_NUB_HW_INDEX;
}

// This should be `std::bit_ceil(aligned_size)` but
// that requires C++20.
// Calculates the smallest integral power of two that is not smaller than x.
static uint64_t bit_ceil(uint64_t input) {
  if (input <= 1 || __builtin_popcount(input) == 1)
    return input;

  return 1ULL << (64 - __builtin_clzll(input));
}

std::vector<DNBArchMachARM64::WatchpointSpec>
DNBArchMachARM64::AlignRequestedWatchpoint(nub_addr_t requested_addr,
                                           nub_size_t requested_size) {

  // Can't watch zero bytes
  if (requested_size == 0)
    return {};

  // Smallest size we can watch on AArch64 is 8 bytes
  constexpr nub_size_t min_watchpoint_alignment = 8;
  nub_size_t aligned_size = std::max(requested_size, min_watchpoint_alignment);

  /// Round up \a requested_size to the next power-of-2 size, at least 8
  /// bytes
  /// requested_size == 8   -> aligned_size == 8
  /// requested_size == 9   -> aligned_size == 16
  aligned_size = aligned_size = bit_ceil(aligned_size);

  nub_addr_t aligned_start = requested_addr & ~(aligned_size - 1);
  // Does this power-of-2 memory range, aligned to power-of-2, completely
  // encompass the requested watch region.
  if (aligned_start + aligned_size >= requested_addr + requested_size) {
    WatchpointSpec wp;
    wp.aligned_start = aligned_start;
    wp.requested_start = requested_addr;
    wp.aligned_size = aligned_size;
    wp.requested_size = requested_size;
    return {{wp}};
  }

  // We need to split this into two watchpoints, split on the aligned_size
  // boundary and re-evaluate the alignment of each half.
  //
  // requested_addr 48 requested_size 20 -> aligned_size 32
  //                              aligned_start 32
  //                              split_addr 64
  //                              first_requested_addr 48
  //                              first_requested_size 16
  //                              second_requested_addr 64
  //                              second_requested_size 4
  nub_addr_t split_addr = aligned_start + aligned_size;

  nub_addr_t first_requested_addr = requested_addr;
  nub_size_t first_requested_size = split_addr - requested_addr;
  nub_addr_t second_requested_addr = split_addr;
  nub_size_t second_requested_size = requested_size - first_requested_size;

  std::vector<WatchpointSpec> first_wp =
      AlignRequestedWatchpoint(first_requested_addr, first_requested_size);
  std::vector<WatchpointSpec> second_wp =
      AlignRequestedWatchpoint(second_requested_addr, second_requested_size);
  if (first_wp.size() != 1 || second_wp.size() != 1)
    return {};

  return {{first_wp[0], second_wp[0]}};
}

uint32_t DNBArchMachARM64::EnableHardwareWatchpoint(nub_addr_t addr,
                                                    nub_size_t size, bool read,
                                                    bool write,
                                                    bool also_set_on_task) {
  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::EnableHardwareWatchpoint(addr = "
                   "0x%8.8llx, size = %zu, read = %u, write = %u)",
                   (uint64_t)addr, size, read, write);

  std::vector<DNBArchMachARM64::WatchpointSpec> wps =
      AlignRequestedWatchpoint(addr, size);
  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::EnableHardwareWatchpoint() using %zu "
                   "hardware watchpoints",
                   wps.size());

  if (wps.size() == 0)
    return INVALID_NUB_HW_INDEX;

  // We must watch for either read or write
  if (read == false && write == false)
    return INVALID_NUB_HW_INDEX;

  // Only one hardware watchpoint needed
  // to implement the user's request.
  if (wps.size() == 1) {
    if (wps[0].aligned_size <= 8)
      return SetBASWatchpoint(wps[0], read, write, also_set_on_task);
    else
      return SetMASKWatchpoint(wps[0], read, write, also_set_on_task);
  }

  // We have multiple WatchpointSpecs

  std::vector<uint32_t> wp_slots_used;
  for (size_t i = 0; i < wps.size(); i++) {
    uint32_t idx =
        EnableHardwareWatchpoint(wps[i].requested_start, wps[i].requested_size,
                                 read, write, also_set_on_task);
    if (idx != INVALID_NUB_HW_INDEX)
      wp_slots_used.push_back(idx);
  }

  // Did we fail to set all of the WatchpointSpecs needed
  // for this user's request?
  if (wps.size() != wp_slots_used.size()) {
    for (int wp_slot : wp_slots_used)
      DisableHardwareWatchpoint(wp_slot, also_set_on_task);
    return INVALID_NUB_HW_INDEX;
  }

  LoHi[wp_slots_used[0]] = wp_slots_used[1];
  return wp_slots_used[0];
}

uint32_t DNBArchMachARM64::SetBASWatchpoint(DNBArchMachARM64::WatchpointSpec wp,
                                            bool read, bool write,
                                            bool also_set_on_task) {
  const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();

  nub_addr_t aligned_dword_addr = wp.aligned_start;
  nub_addr_t watching_offset = wp.requested_start - wp.aligned_start;
  nub_size_t watching_size = wp.requested_size;

  // If user asks to watch 3 bytes at 0x1005,
  // aligned_dword_addr 0x1000
  // watching_offset 5
  // watching_size 3

  // Set the Byte Address Selects bits DBGWCRn_EL1 bits [12:5] based on the
  // above.
  // The bit shift and negation operation will give us 0b11 for 2, 0b1111 for 4,
  // etc, up to 0b11111111 for 8.
  // then we shift those bits left by the offset into this dword that we are
  // interested in.
  // e.g. if we are watching bytes 4,5,6,7 in a dword we want a BAS of
  // 0b11110000.
  uint32_t byte_address_select = ((1 << watching_size) - 1) << watching_offset;

  // Read the debug state
  kern_return_t kret = GetDBGState(false);
  if (kret != KERN_SUCCESS)
    return INVALID_NUB_HW_INDEX;

  // Check to make sure we have the needed hardware support
  uint32_t i = 0;

  for (i = 0; i < num_hw_watchpoints; ++i) {
    if ((m_state.dbg.__wcr[i] & WCR_ENABLE) == 0)
      break; // We found an available hw watchpoint slot
  }
  if (i == num_hw_watchpoints) {
    DNBLogThreadedIf(LOG_WATCHPOINTS,
                     "DNBArchMachARM64::"
                     "SetBASWatchpoint(): All "
                     "hardware resources (%u) are in use.",
                     num_hw_watchpoints);
    return INVALID_NUB_HW_INDEX;
  }

  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::"
                   "SetBASWatchpoint() "
                   "set hardware register %d to BAS watchpoint "
                   "aligned start address 0x%llx, watch region start "
                   "offset %lld, number of bytes %zu",
                   i, aligned_dword_addr, watching_offset, watching_size);

  // Clear any previous LoHi joined-watchpoint that may have been in use
  LoHi[i] = 0;

  // shift our Byte Address Select bits up to the correct bit range for the
  // DBGWCRn_EL1
  byte_address_select = byte_address_select << 5;

  // Make sure bits 1:0 are clear in our address
  m_state.dbg.__wvr[i] = aligned_dword_addr;       // DVA (Data Virtual Address)
  m_state.dbg.__wcr[i] = byte_address_select |     // Which bytes that follow
                                                   // the DVA that we will watch
                         S_USER |                  // Stop only in user mode
                         (read ? WCR_LOAD : 0) |   // Stop on read access?
                         (write ? WCR_STORE : 0) | // Stop on write access?
                         WCR_ENABLE;               // Enable this watchpoint;

  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::SetBASWatchpoint() "
                   "adding watchpoint on address 0x%llx with control "
                   "register value 0x%x",
                   (uint64_t)m_state.dbg.__wvr[i],
                   (uint32_t)m_state.dbg.__wcr[i]);

  kret = SetDBGState(also_set_on_task);
  // DumpDBGState(m_state.dbg);

  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::"
                   "SetBASWatchpoint() "
                   "SetDBGState() => 0x%8.8x.",
                   kret);

  if (kret == KERN_SUCCESS)
    return i;

  return INVALID_NUB_HW_INDEX;
}

uint32_t
DNBArchMachARM64::SetMASKWatchpoint(DNBArchMachARM64::WatchpointSpec wp,
                                    bool read, bool write,
                                    bool also_set_on_task) {
  const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();

  // Read the debug state
  kern_return_t kret = GetDBGState(false);
  if (kret != KERN_SUCCESS)
    return INVALID_NUB_HW_INDEX;

  // Check to make sure we have the needed hardware support
  uint32_t i = 0;

  for (i = 0; i < num_hw_watchpoints; ++i) {
    if ((m_state.dbg.__wcr[i] & WCR_ENABLE) == 0)
      break; // We found an available hw watchpoint slot
  }
  if (i == num_hw_watchpoints) {
    DNBLogThreadedIf(LOG_WATCHPOINTS,
                     "DNBArchMachARM64::"
                     "SetMASKWatchpoint(): All "
                     "hardware resources (%u) are in use.",
                     num_hw_watchpoints);
    return INVALID_NUB_HW_INDEX;
  }

  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::"
                   "SetMASKWatchpoint() "
                   "set hardware register %d to MASK watchpoint "
                   "aligned start address 0x%llx, aligned size %zu",
                   i, wp.aligned_start, wp.aligned_size);

  // Clear any previous LoHi joined-watchpoint that may have been in use
  LoHi[i] = 0;

  // MASK field is the number of low bits that are masked off
  // when comparing the address with the DBGWVR<n>_EL1 values.
  // If aligned size is 16, that means we ignore low 4 bits, 0b1111.
  // popcount(16 - 1) give us the correct value of 4.
  // 2GB is max watchable region, which is 31 bits (low bits 0x7fffffff
  // masked off) -- a MASK value of 31.
  const uint64_t mask = __builtin_popcountl(wp.aligned_size - 1) << 24;
  // A '0b11111111' BAS value needed for mask watchpoints plus a
  // nonzero mask value.
  const uint64_t not_bas_wp = 0xff << 5;

  m_state.dbg.__wvr[i] = wp.aligned_start;
  m_state.dbg.__wcr[i] = mask | not_bas_wp | S_USER | // Stop only in user mode
                         (read ? WCR_LOAD : 0) |      // Stop on read access?
                         (write ? WCR_STORE : 0) |    // Stop on write access?
                         WCR_ENABLE;                  // Enable this watchpoint;

  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::SetMASKWatchpoint() "
                   "adding watchpoint on address 0x%llx with control "
                   "register value 0x%llx",
                   (uint64_t)m_state.dbg.__wvr[i],
                   (uint64_t)m_state.dbg.__wcr[i]);

  kret = SetDBGState(also_set_on_task);

  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::"
                   "SetMASKWatchpoint() "
                   "SetDBGState() => 0x%8.8x.",
                   kret);

  if (kret == KERN_SUCCESS)
    return i;

  return INVALID_NUB_HW_INDEX;
}

bool DNBArchMachARM64::ReenableHardwareWatchpoint(uint32_t hw_index) {
  // If this logical watchpoint # is actually implemented using
  // two hardware watchpoint registers, re-enable both of them.

  if (hw_index < NumSupportedHardwareWatchpoints() && LoHi[hw_index]) {
    return ReenableHardwareWatchpoint_helper(hw_index) &&
           ReenableHardwareWatchpoint_helper(LoHi[hw_index]);
  } else {
    return ReenableHardwareWatchpoint_helper(hw_index);
  }
}

bool DNBArchMachARM64::ReenableHardwareWatchpoint_helper(uint32_t hw_index) {
  kern_return_t kret = GetDBGState(false);
  if (kret != KERN_SUCCESS)
    return false;

  const uint32_t num_hw_points = NumSupportedHardwareWatchpoints();
  if (hw_index >= num_hw_points)
    return false;

  m_state.dbg.__wvr[hw_index] = m_disabled_watchpoints[hw_index].addr;
  m_state.dbg.__wcr[hw_index] = m_disabled_watchpoints[hw_index].control;

  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::"
                   "ReenableHardwareWatchpoint_helper( %u ) - WVR%u = "
                   "0x%8.8llx  WCR%u = 0x%8.8llx",
                   hw_index, hw_index, (uint64_t)m_state.dbg.__wvr[hw_index],
                   hw_index, (uint64_t)m_state.dbg.__wcr[hw_index]);

  kret = SetDBGState(false);

  return (kret == KERN_SUCCESS);
}

bool DNBArchMachARM64::DisableHardwareWatchpoint(uint32_t hw_index,
                                                 bool also_set_on_task) {
  if (hw_index < NumSupportedHardwareWatchpoints() && LoHi[hw_index]) {
    return DisableHardwareWatchpoint_helper(hw_index, also_set_on_task) &&
           DisableHardwareWatchpoint_helper(LoHi[hw_index], also_set_on_task);
  } else {
    return DisableHardwareWatchpoint_helper(hw_index, also_set_on_task);
  }
}

bool DNBArchMachARM64::DisableHardwareWatchpoint_helper(uint32_t hw_index,
                                                        bool also_set_on_task) {
  kern_return_t kret = GetDBGState(false);
  if (kret != KERN_SUCCESS)
    return false;

  const uint32_t num_hw_points = NumSupportedHardwareWatchpoints();
  if (hw_index >= num_hw_points)
    return false;

  m_disabled_watchpoints[hw_index].addr = m_state.dbg.__wvr[hw_index];
  m_disabled_watchpoints[hw_index].control = m_state.dbg.__wcr[hw_index];

  m_state.dbg.__wcr[hw_index] &= ~((nub_addr_t)WCR_ENABLE);
  DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::"
                                    "DisableHardwareWatchpoint( %u ) - WVR%u = "
                                    "0x%8.8llx  WCR%u = 0x%8.8llx",
                   hw_index, hw_index, (uint64_t)m_state.dbg.__wvr[hw_index],
                   hw_index, (uint64_t)m_state.dbg.__wcr[hw_index]);

  kret = SetDBGState(also_set_on_task);

  return (kret == KERN_SUCCESS);
}

bool DNBArchMachARM64::DisableHardwareBreakpoint(uint32_t hw_index,
                                                 bool also_set_on_task) {
  kern_return_t kret = GetDBGState(false);
  if (kret != KERN_SUCCESS)
    return false;

  const uint32_t num_hw_points = NumSupportedHardwareBreakpoints();
  if (hw_index >= num_hw_points)
    return false;

  m_disabled_breakpoints[hw_index].addr = m_state.dbg.__bvr[hw_index];
  m_disabled_breakpoints[hw_index].control = m_state.dbg.__bcr[hw_index];

  m_state.dbg.__bcr[hw_index] = 0;
  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::"
                   "DisableHardwareBreakpoint( %u ) - WVR%u = "
                   "0x%8.8llx  BCR%u = 0x%8.8llx",
                   hw_index, hw_index, (uint64_t)m_state.dbg.__bvr[hw_index],
                   hw_index, (uint64_t)m_state.dbg.__bcr[hw_index]);

  kret = SetDBGState(also_set_on_task);

  return (kret == KERN_SUCCESS);
}

// This is for checking the Byte Address Select bits in the DBRWCRn_EL1 control
// register.
// Returns -1 if the trailing bit patterns are not one of:
// { 0b???????1, 0b??????10, 0b?????100, 0b????1000, 0b???10000, 0b??100000,
// 0b?1000000, 0b10000000 }.
static inline int32_t LowestBitSet(uint32_t val) {
  for (unsigned i = 0; i < 8; ++i) {
    if (bit(val, i))
      return i;
  }
  return -1;
}

// Iterate through the debug registers; return the index of the first watchpoint
// whose address matches.
// As a side effect, the starting address as understood by the debugger is
// returned which could be
// different from 'addr' passed as an in/out argument.
uint32_t DNBArchMachARM64::GetHardwareWatchpointHit(nub_addr_t &addr) {
  // Read the debug state
  kern_return_t kret = GetDBGState(true);
  // DumpDBGState(m_state.dbg);
  DNBLogThreadedIf(
      LOG_WATCHPOINTS,
      "DNBArchMachARM64::GetHardwareWatchpointHit() GetDBGState() => 0x%8.8x.",
      kret);
  DNBLogThreadedIf(LOG_WATCHPOINTS,
                   "DNBArchMachARM64::GetHardwareWatchpointHit() addr = 0x%llx",
                   (uint64_t)addr);

  if (kret == KERN_SUCCESS) {
    DBG &debug_state = m_state.dbg;
    uint32_t i, num = NumSupportedHardwareWatchpoints();
    for (i = 0; i < num; ++i) {
      nub_addr_t wp_addr = GetWatchAddress(debug_state, i);

      DNBLogThreadedIf(LOG_WATCHPOINTS,
                       "DNBArchImplARM64::"
                       "GetHardwareWatchpointHit() slot: %u "
                       "(addr = 0x%llx, WCR = 0x%llx)",
                       i, wp_addr, debug_state.__wcr[i]);

      if (!IsWatchpointEnabled(debug_state, i))
        continue;

      // DBGWCR<n>EL1.BAS are the bits of the doubleword that are watched
      // with a BAS watchpoint.
      uint32_t bas_bits = bits(debug_state.__wcr[i], 12, 5);
      // DBGWCR<n>EL1.MASK is the number of bits that are masked off the
      // virtual address when comparing to DBGWVR<n>_EL1.
      uint32_t mask = bits(debug_state.__wcr[i], 28, 24);

      const bool is_bas_watchpoint = mask == 0;

      DNBLogThreadedIf(
          LOG_WATCHPOINTS,
          "DNBArchImplARM64::"
          "GetHardwareWatchpointHit() slot: %u %s",
          i, is_bas_watchpoint ? "is BAS watchpoint" : "is MASK watchpoint");

      if (is_bas_watchpoint) {
        if (bits(wp_addr, 48, 3) != bits(addr, 48, 3))
          continue;
      } else {
        if (bits(wp_addr, 48, mask) == bits(addr, 48, mask)) {
          DNBLogThreadedIf(LOG_WATCHPOINTS,
                           "DNBArchImplARM64::"
                           "GetHardwareWatchpointHit() slot: %u matched MASK "
                           "ignoring %u low bits",
                           i, mask);
          return i;
        }
      }

      if (is_bas_watchpoint) {
        // Sanity check the bas_bits
        uint32_t lsb = LowestBitSet(bas_bits);
        if (lsb < 0)
          continue;

        uint64_t byte_to_match = bits(addr, 2, 0);

        if (bas_bits & (1 << byte_to_match)) {
          addr = wp_addr + lsb;
          DNBLogThreadedIf(LOG_WATCHPOINTS,
                           "DNBArchImplARM64::"
                           "GetHardwareWatchpointHit() slot: %u matched BAS",
                           i);
          return i;
        }
      }
    }
  }
  return INVALID_NUB_HW_INDEX;
}

nub_addr_t DNBArchMachARM64::GetWatchpointAddressByIndex(uint32_t hw_index) {
  kern_return_t kret = GetDBGState(true);
  if (kret != KERN_SUCCESS)
    return INVALID_NUB_ADDRESS;
  const uint32_t num = NumSupportedHardwareWatchpoints();
  if (hw_index >= num)
    return INVALID_NUB_ADDRESS;
  if (IsWatchpointEnabled(m_state.dbg, hw_index))
    return GetWatchAddress(m_state.dbg, hw_index);
  return INVALID_NUB_ADDRESS;
}

bool DNBArchMachARM64::IsWatchpointEnabled(const DBG &debug_state,
                                           uint32_t hw_index) {
  // Watchpoint Control Registers, bitfield definitions
  // ...
  // Bits    Value    Description
  // [0]     0        Watchpoint disabled
  //         1        Watchpoint enabled.
  return (debug_state.__wcr[hw_index] & 1u);
}

nub_addr_t DNBArchMachARM64::GetWatchAddress(const DBG &debug_state,
                                             uint32_t hw_index) {
  // Watchpoint Value Registers, bitfield definitions
  // Bits        Description
  // [31:2]      Watchpoint value (word address, i.e., 4-byte aligned)
  // [1:0]       RAZ/SBZP
  return bits(debug_state.__wvr[hw_index], 63, 0);
}

// Register information definitions for 64 bit ARMv8.
enum gpr_regnums {
  gpr_x0 = 0,
  gpr_x1,
  gpr_x2,
  gpr_x3,
  gpr_x4,
  gpr_x5,
  gpr_x6,
  gpr_x7,
  gpr_x8,
  gpr_x9,
  gpr_x10,
  gpr_x11,
  gpr_x12,
  gpr_x13,
  gpr_x14,
  gpr_x15,
  gpr_x16,
  gpr_x17,
  gpr_x18,
  gpr_x19,
  gpr_x20,
  gpr_x21,
  gpr_x22,
  gpr_x23,
  gpr_x24,
  gpr_x25,
  gpr_x26,
  gpr_x27,
  gpr_x28,
  gpr_fp,
  gpr_x29 = gpr_fp,
  gpr_lr,
  gpr_x30 = gpr_lr,
  gpr_sp,
  gpr_x31 = gpr_sp,
  gpr_pc,
  gpr_cpsr,
  gpr_w0,
  gpr_w1,
  gpr_w2,
  gpr_w3,
  gpr_w4,
  gpr_w5,
  gpr_w6,
  gpr_w7,
  gpr_w8,
  gpr_w9,
  gpr_w10,
  gpr_w11,
  gpr_w12,
  gpr_w13,
  gpr_w14,
  gpr_w15,
  gpr_w16,
  gpr_w17,
  gpr_w18,
  gpr_w19,
  gpr_w20,
  gpr_w21,
  gpr_w22,
  gpr_w23,
  gpr_w24,
  gpr_w25,
  gpr_w26,
  gpr_w27,
  gpr_w28

};

enum {
  vfp_v0 = 0,
  vfp_v1,
  vfp_v2,
  vfp_v3,
  vfp_v4,
  vfp_v5,
  vfp_v6,
  vfp_v7,
  vfp_v8,
  vfp_v9,
  vfp_v10,
  vfp_v11,
  vfp_v12,
  vfp_v13,
  vfp_v14,
  vfp_v15,
  vfp_v16,
  vfp_v17,
  vfp_v18,
  vfp_v19,
  vfp_v20,
  vfp_v21,
  vfp_v22,
  vfp_v23,
  vfp_v24,
  vfp_v25,
  vfp_v26,
  vfp_v27,
  vfp_v28,
  vfp_v29,
  vfp_v30,
  vfp_v31,
  vfp_fpsr,
  vfp_fpcr,

  // lower 32 bits of the corresponding vfp_v<n> reg.
  vfp_s0,
  vfp_s1,
  vfp_s2,
  vfp_s3,
  vfp_s4,
  vfp_s5,
  vfp_s6,
  vfp_s7,
  vfp_s8,
  vfp_s9,
  vfp_s10,
  vfp_s11,
  vfp_s12,
  vfp_s13,
  vfp_s14,
  vfp_s15,
  vfp_s16,
  vfp_s17,
  vfp_s18,
  vfp_s19,
  vfp_s20,
  vfp_s21,
  vfp_s22,
  vfp_s23,
  vfp_s24,
  vfp_s25,
  vfp_s26,
  vfp_s27,
  vfp_s28,
  vfp_s29,
  vfp_s30,
  vfp_s31,

  // lower 64 bits of the corresponding vfp_v<n> reg.
  vfp_d0,
  vfp_d1,
  vfp_d2,
  vfp_d3,
  vfp_d4,
  vfp_d5,
  vfp_d6,
  vfp_d7,
  vfp_d8,
  vfp_d9,
  vfp_d10,
  vfp_d11,
  vfp_d12,
  vfp_d13,
  vfp_d14,
  vfp_d15,
  vfp_d16,
  vfp_d17,
  vfp_d18,
  vfp_d19,
  vfp_d20,
  vfp_d21,
  vfp_d22,
  vfp_d23,
  vfp_d24,
  vfp_d25,
  vfp_d26,
  vfp_d27,
  vfp_d28,
  vfp_d29,
  vfp_d30,
  vfp_d31
};

enum { exc_far = 0, exc_esr, exc_exception };

// These numbers from the "DWARF for the ARM 64-bit Architecture (AArch64)"
// document.

enum {
  dwarf_x0 = 0,
  dwarf_x1,
  dwarf_x2,
  dwarf_x3,
  dwarf_x4,
  dwarf_x5,
  dwarf_x6,
  dwarf_x7,
  dwarf_x8,
  dwarf_x9,
  dwarf_x10,
  dwarf_x11,
  dwarf_x12,
  dwarf_x13,
  dwarf_x14,
  dwarf_x15,
  dwarf_x16,
  dwarf_x17,
  dwarf_x18,
  dwarf_x19,
  dwarf_x20,
  dwarf_x21,
  dwarf_x22,
  dwarf_x23,
  dwarf_x24,
  dwarf_x25,
  dwarf_x26,
  dwarf_x27,
  dwarf_x28,
  dwarf_x29,
  dwarf_x30,
  dwarf_x31,
  dwarf_pc = 32,
  dwarf_elr_mode = 33,
  dwarf_fp = dwarf_x29,
  dwarf_lr = dwarf_x30,
  dwarf_sp = dwarf_x31,
  // 34-63 reserved

  // V0-V31 (128 bit vector registers)
  dwarf_v0 = 64,
  dwarf_v1,
  dwarf_v2,
  dwarf_v3,
  dwarf_v4,
  dwarf_v5,
  dwarf_v6,
  dwarf_v7,
  dwarf_v8,
  dwarf_v9,
  dwarf_v10,
  dwarf_v11,
  dwarf_v12,
  dwarf_v13,
  dwarf_v14,
  dwarf_v15,
  dwarf_v16,
  dwarf_v17,
  dwarf_v18,
  dwarf_v19,
  dwarf_v20,
  dwarf_v21,
  dwarf_v22,
  dwarf_v23,
  dwarf_v24,
  dwarf_v25,
  dwarf_v26,
  dwarf_v27,
  dwarf_v28,
  dwarf_v29,
  dwarf_v30,
  dwarf_v31

  // 96-127 reserved
};

enum {
  debugserver_gpr_x0 = 0,
  debugserver_gpr_x1,
  debugserver_gpr_x2,
  debugserver_gpr_x3,
  debugserver_gpr_x4,
  debugserver_gpr_x5,
  debugserver_gpr_x6,
  debugserver_gpr_x7,
  debugserver_gpr_x8,
  debugserver_gpr_x9,
  debugserver_gpr_x10,
  debugserver_gpr_x11,
  debugserver_gpr_x12,
  debugserver_gpr_x13,
  debugserver_gpr_x14,
  debugserver_gpr_x15,
  debugserver_gpr_x16,
  debugserver_gpr_x17,
  debugserver_gpr_x18,
  debugserver_gpr_x19,
  debugserver_gpr_x20,
  debugserver_gpr_x21,
  debugserver_gpr_x22,
  debugserver_gpr_x23,
  debugserver_gpr_x24,
  debugserver_gpr_x25,
  debugserver_gpr_x26,
  debugserver_gpr_x27,
  debugserver_gpr_x28,
  debugserver_gpr_fp, // x29
  debugserver_gpr_lr, // x30
  debugserver_gpr_sp, // sp aka xsp
  debugserver_gpr_pc,
  debugserver_gpr_cpsr,
  debugserver_vfp_v0,
  debugserver_vfp_v1,
  debugserver_vfp_v2,
  debugserver_vfp_v3,
  debugserver_vfp_v4,
  debugserver_vfp_v5,
  debugserver_vfp_v6,
  debugserver_vfp_v7,
  debugserver_vfp_v8,
  debugserver_vfp_v9,
  debugserver_vfp_v10,
  debugserver_vfp_v11,
  debugserver_vfp_v12,
  debugserver_vfp_v13,
  debugserver_vfp_v14,
  debugserver_vfp_v15,
  debugserver_vfp_v16,
  debugserver_vfp_v17,
  debugserver_vfp_v18,
  debugserver_vfp_v19,
  debugserver_vfp_v20,
  debugserver_vfp_v21,
  debugserver_vfp_v22,
  debugserver_vfp_v23,
  debugserver_vfp_v24,
  debugserver_vfp_v25,
  debugserver_vfp_v26,
  debugserver_vfp_v27,
  debugserver_vfp_v28,
  debugserver_vfp_v29,
  debugserver_vfp_v30,
  debugserver_vfp_v31,
  debugserver_vfp_fpsr,
  debugserver_vfp_fpcr
};

const char *g_contained_x0[]{"x0", NULL};
const char *g_contained_x1[]{"x1", NULL};
const char *g_contained_x2[]{"x2", NULL};
const char *g_contained_x3[]{"x3", NULL};
const char *g_contained_x4[]{"x4", NULL};
const char *g_contained_x5[]{"x5", NULL};
const char *g_contained_x6[]{"x6", NULL};
const char *g_contained_x7[]{"x7", NULL};
const char *g_contained_x8[]{"x8", NULL};
const char *g_contained_x9[]{"x9", NULL};
const char *g_contained_x10[]{"x10", NULL};
const char *g_contained_x11[]{"x11", NULL};
const char *g_contained_x12[]{"x12", NULL};
const char *g_contained_x13[]{"x13", NULL};
const char *g_contained_x14[]{"x14", NULL};
const char *g_contained_x15[]{"x15", NULL};
const char *g_contained_x16[]{"x16", NULL};
const char *g_contained_x17[]{"x17", NULL};
const char *g_contained_x18[]{"x18", NULL};
const char *g_contained_x19[]{"x19", NULL};
const char *g_contained_x20[]{"x20", NULL};
const char *g_contained_x21[]{"x21", NULL};
const char *g_contained_x22[]{"x22", NULL};
const char *g_contained_x23[]{"x23", NULL};
const char *g_contained_x24[]{"x24", NULL};
const char *g_contained_x25[]{"x25", NULL};
const char *g_contained_x26[]{"x26", NULL};
const char *g_contained_x27[]{"x27", NULL};
const char *g_contained_x28[]{"x28", NULL};

const char *g_invalidate_x0[]{"x0", "w0", NULL};
const char *g_invalidate_x1[]{"x1", "w1", NULL};
const char *g_invalidate_x2[]{"x2", "w2", NULL};
const char *g_invalidate_x3[]{"x3", "w3", NULL};
const char *g_invalidate_x4[]{"x4", "w4", NULL};
const char *g_invalidate_x5[]{"x5", "w5", NULL};
const char *g_invalidate_x6[]{"x6", "w6", NULL};
const char *g_invalidate_x7[]{"x7", "w7", NULL};
const char *g_invalidate_x8[]{"x8", "w8", NULL};
const char *g_invalidate_x9[]{"x9", "w9", NULL};
const char *g_invalidate_x10[]{"x10", "w10", NULL};
const char *g_invalidate_x11[]{"x11", "w11", NULL};
const char *g_invalidate_x12[]{"x12", "w12", NULL};
const char *g_invalidate_x13[]{"x13", "w13", NULL};
const char *g_invalidate_x14[]{"x14", "w14", NULL};
const char *g_invalidate_x15[]{"x15", "w15", NULL};
const char *g_invalidate_x16[]{"x16", "w16", NULL};
const char *g_invalidate_x17[]{"x17", "w17", NULL};
const char *g_invalidate_x18[]{"x18", "w18", NULL};
const char *g_invalidate_x19[]{"x19", "w19", NULL};
const char *g_invalidate_x20[]{"x20", "w20", NULL};
const char *g_invalidate_x21[]{"x21", "w21", NULL};
const char *g_invalidate_x22[]{"x22", "w22", NULL};
const char *g_invalidate_x23[]{"x23", "w23", NULL};
const char *g_invalidate_x24[]{"x24", "w24", NULL};
const char *g_invalidate_x25[]{"x25", "w25", NULL};
const char *g_invalidate_x26[]{"x26", "w26", NULL};
const char *g_invalidate_x27[]{"x27", "w27", NULL};
const char *g_invalidate_x28[]{"x28", "w28", NULL};

#define GPR_OFFSET_IDX(idx) (offsetof(DNBArchMachARM64::GPR, __x[idx]))

#define GPR_OFFSET_NAME(reg) (offsetof(DNBArchMachARM64::GPR, __##reg))

// These macros will auto define the register name, alt name, register size,
// register offset, encoding, format and native register. This ensures that
// the register state structures are defined correctly and have the correct
// sizes and offsets.
#define DEFINE_GPR_IDX(idx, reg, alt, gen)                                     \
  {                                                                            \
    e_regSetGPR, gpr_##reg, #reg, alt, Uint, Hex, 8, GPR_OFFSET_IDX(idx),      \
        dwarf_##reg, dwarf_##reg, gen, debugserver_gpr_##reg, NULL,            \
        g_invalidate_x##idx                                                    \
  }
#define DEFINE_GPR_NAME(reg, alt, gen)                                         \
  {                                                                            \
    e_regSetGPR, gpr_##reg, #reg, alt, Uint, Hex, 8, GPR_OFFSET_NAME(reg),     \
        dwarf_##reg, dwarf_##reg, gen, debugserver_gpr_##reg, NULL, NULL       \
  }
#define DEFINE_PSEUDO_GPR_IDX(idx, reg)                                        \
  {                                                                            \
    e_regSetGPR, gpr_##reg, #reg, NULL, Uint, Hex, 4, 0, INVALID_NUB_REGNUM,   \
        INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,            \
        g_contained_x##idx, g_invalidate_x##idx                                \
  }

//_STRUCT_ARM_THREAD_STATE64
//{
//	uint64_t    x[29];	/* General purpose registers x0-x28 */
//	uint64_t    fp;		/* Frame pointer x29 */
//	uint64_t    lr;		/* Link register x30 */
//	uint64_t    sp;		/* Stack pointer x31 */
//	uint64_t    pc;		/* Program counter */
//	uint32_t    cpsr;	/* Current program status register */
//};

// General purpose registers
const DNBRegisterInfo DNBArchMachARM64::g_gpr_registers[] = {
    DEFINE_GPR_IDX(0, x0, "arg1", GENERIC_REGNUM_ARG1),
    DEFINE_GPR_IDX(1, x1, "arg2", GENERIC_REGNUM_ARG2),
    DEFINE_GPR_IDX(2, x2, "arg3", GENERIC_REGNUM_ARG3),
    DEFINE_GPR_IDX(3, x3, "arg4", GENERIC_REGNUM_ARG4),
    DEFINE_GPR_IDX(4, x4, "arg5", GENERIC_REGNUM_ARG5),
    DEFINE_GPR_IDX(5, x5, "arg6", GENERIC_REGNUM_ARG6),
    DEFINE_GPR_IDX(6, x6, "arg7", GENERIC_REGNUM_ARG7),
    DEFINE_GPR_IDX(7, x7, "arg8", GENERIC_REGNUM_ARG8),
    DEFINE_GPR_IDX(8, x8, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(9, x9, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(10, x10, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(11, x11, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(12, x12, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(13, x13, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(14, x14, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(15, x15, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(16, x16, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(17, x17, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(18, x18, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(19, x19, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(20, x20, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(21, x21, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(22, x22, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(23, x23, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(24, x24, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(25, x25, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(26, x26, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(27, x27, NULL, INVALID_NUB_REGNUM),
    DEFINE_GPR_IDX(28, x28, NULL, INVALID_NUB_REGNUM),
    // For the G/g packet we want to show where the offset into the regctx
    // is for fp/lr/sp/pc, but we cannot directly access them on arm64e
    // devices (and therefore can't offsetof() them)) - add the offset based
    // on the last accessible register by hand for advertising the location
    // in the regctx to lldb.  We'll go through the accessor functions when
    // we read/write them here.
    {
       e_regSetGPR, gpr_fp, "fp", "x29", Uint, Hex, 8, GPR_OFFSET_IDX(28) + 8,
       dwarf_fp, dwarf_fp, GENERIC_REGNUM_FP, debugserver_gpr_fp, NULL, NULL
    },
    {
       e_regSetGPR, gpr_lr, "lr", "x30", Uint, Hex, 8, GPR_OFFSET_IDX(28) + 16,
       dwarf_lr, dwarf_lr, GENERIC_REGNUM_RA, debugserver_gpr_lr, NULL, NULL
    },
    {
       e_regSetGPR, gpr_sp, "sp", "xsp", Uint, Hex, 8, GPR_OFFSET_IDX(28) + 24,
       dwarf_sp, dwarf_sp, GENERIC_REGNUM_SP, debugserver_gpr_sp, NULL, NULL
    },
    {
       e_regSetGPR, gpr_pc, "pc", NULL, Uint, Hex, 8, GPR_OFFSET_IDX(28) + 32,
       dwarf_pc, dwarf_pc, GENERIC_REGNUM_PC, debugserver_gpr_pc, NULL, NULL
    },

    // in armv7 we specify that writing to the CPSR should invalidate r8-12, sp,
    // lr.
    // this should be specified for arm64 too even though debugserver is only
    // used for
    // userland debugging.
    {e_regSetGPR, gpr_cpsr, "cpsr", "flags", Uint, Hex, 4,
     GPR_OFFSET_NAME(cpsr), dwarf_elr_mode, dwarf_elr_mode, GENERIC_REGNUM_FLAGS,
     debugserver_gpr_cpsr, NULL, NULL},

    DEFINE_PSEUDO_GPR_IDX(0, w0),
    DEFINE_PSEUDO_GPR_IDX(1, w1),
    DEFINE_PSEUDO_GPR_IDX(2, w2),
    DEFINE_PSEUDO_GPR_IDX(3, w3),
    DEFINE_PSEUDO_GPR_IDX(4, w4),
    DEFINE_PSEUDO_GPR_IDX(5, w5),
    DEFINE_PSEUDO_GPR_IDX(6, w6),
    DEFINE_PSEUDO_GPR_IDX(7, w7),
    DEFINE_PSEUDO_GPR_IDX(8, w8),
    DEFINE_PSEUDO_GPR_IDX(9, w9),
    DEFINE_PSEUDO_GPR_IDX(10, w10),
    DEFINE_PSEUDO_GPR_IDX(11, w11),
    DEFINE_PSEUDO_GPR_IDX(12, w12),
    DEFINE_PSEUDO_GPR_IDX(13, w13),
    DEFINE_PSEUDO_GPR_IDX(14, w14),
    DEFINE_PSEUDO_GPR_IDX(15, w15),
    DEFINE_PSEUDO_GPR_IDX(16, w16),
    DEFINE_PSEUDO_GPR_IDX(17, w17),
    DEFINE_PSEUDO_GPR_IDX(18, w18),
    DEFINE_PSEUDO_GPR_IDX(19, w19),
    DEFINE_PSEUDO_GPR_IDX(20, w20),
    DEFINE_PSEUDO_GPR_IDX(21, w21),
    DEFINE_PSEUDO_GPR_IDX(22, w22),
    DEFINE_PSEUDO_GPR_IDX(23, w23),
    DEFINE_PSEUDO_GPR_IDX(24, w24),
    DEFINE_PSEUDO_GPR_IDX(25, w25),
    DEFINE_PSEUDO_GPR_IDX(26, w26),
    DEFINE_PSEUDO_GPR_IDX(27, w27),
    DEFINE_PSEUDO_GPR_IDX(28, w28)};

const char *g_contained_v0[]{"v0", NULL};
const char *g_contained_v1[]{"v1", NULL};
const char *g_contained_v2[]{"v2", NULL};
const char *g_contained_v3[]{"v3", NULL};
const char *g_contained_v4[]{"v4", NULL};
const char *g_contained_v5[]{"v5", NULL};
const char *g_contained_v6[]{"v6", NULL};
const char *g_contained_v7[]{"v7", NULL};
const char *g_contained_v8[]{"v8", NULL};
const char *g_contained_v9[]{"v9", NULL};
const char *g_contained_v10[]{"v10", NULL};
const char *g_contained_v11[]{"v11", NULL};
const char *g_contained_v12[]{"v12", NULL};
const char *g_contained_v13[]{"v13", NULL};
const char *g_contained_v14[]{"v14", NULL};
const char *g_contained_v15[]{"v15", NULL};
const char *g_contained_v16[]{"v16", NULL};
const char *g_contained_v17[]{"v17", NULL};
const char *g_contained_v18[]{"v18", NULL};
const char *g_contained_v19[]{"v19", NULL};
const char *g_contained_v20[]{"v20", NULL};
const char *g_contained_v21[]{"v21", NULL};
const char *g_contained_v22[]{"v22", NULL};
const char *g_contained_v23[]{"v23", NULL};
const char *g_contained_v24[]{"v24", NULL};
const char *g_contained_v25[]{"v25", NULL};
const char *g_contained_v26[]{"v26", NULL};
const char *g_contained_v27[]{"v27", NULL};
const char *g_contained_v28[]{"v28", NULL};
const char *g_contained_v29[]{"v29", NULL};
const char *g_contained_v30[]{"v30", NULL};
const char *g_contained_v31[]{"v31", NULL};

const char *g_invalidate_v0[]{"v0", "d0", "s0", NULL};
const char *g_invalidate_v1[]{"v1", "d1", "s1", NULL};
const char *g_invalidate_v2[]{"v2", "d2", "s2", NULL};
const char *g_invalidate_v3[]{"v3", "d3", "s3", NULL};
const char *g_invalidate_v4[]{"v4", "d4", "s4", NULL};
const char *g_invalidate_v5[]{"v5", "d5", "s5", NULL};
const char *g_invalidate_v6[]{"v6", "d6", "s6", NULL};
const char *g_invalidate_v7[]{"v7", "d7", "s7", NULL};
const char *g_invalidate_v8[]{"v8", "d8", "s8", NULL};
const char *g_invalidate_v9[]{"v9", "d9", "s9", NULL};
const char *g_invalidate_v10[]{"v10", "d10", "s10", NULL};
const char *g_invalidate_v11[]{"v11", "d11", "s11", NULL};
const char *g_invalidate_v12[]{"v12", "d12", "s12", NULL};
const char *g_invalidate_v13[]{"v13", "d13", "s13", NULL};
const char *g_invalidate_v14[]{"v14", "d14", "s14", NULL};
const char *g_invalidate_v15[]{"v15", "d15", "s15", NULL};
const char *g_invalidate_v16[]{"v16", "d16", "s16", NULL};
const char *g_invalidate_v17[]{"v17", "d17", "s17", NULL};
const char *g_invalidate_v18[]{"v18", "d18", "s18", NULL};
const char *g_invalidate_v19[]{"v19", "d19", "s19", NULL};
const char *g_invalidate_v20[]{"v20", "d20", "s20", NULL};
const char *g_invalidate_v21[]{"v21", "d21", "s21", NULL};
const char *g_invalidate_v22[]{"v22", "d22", "s22", NULL};
const char *g_invalidate_v23[]{"v23", "d23", "s23", NULL};
const char *g_invalidate_v24[]{"v24", "d24", "s24", NULL};
const char *g_invalidate_v25[]{"v25", "d25", "s25", NULL};
const char *g_invalidate_v26[]{"v26", "d26", "s26", NULL};
const char *g_invalidate_v27[]{"v27", "d27", "s27", NULL};
const char *g_invalidate_v28[]{"v28", "d28", "s28", NULL};
const char *g_invalidate_v29[]{"v29", "d29", "s29", NULL};
const char *g_invalidate_v30[]{"v30", "d30", "s30", NULL};
const char *g_invalidate_v31[]{"v31", "d31", "s31", NULL};

#if defined(__arm64__) || defined(__aarch64__)
#define VFP_V_OFFSET_IDX(idx)                                                  \
  (offsetof(DNBArchMachARM64::FPU, __v) + (idx * 16) +                         \
   offsetof(DNBArchMachARM64::Context, vfp))
#else
#define VFP_V_OFFSET_IDX(idx)                                                  \
  (offsetof(DNBArchMachARM64::FPU, opaque) + (idx * 16) +                      \
   offsetof(DNBArchMachARM64::Context, vfp))
#endif
#define VFP_OFFSET_NAME(reg)                                                   \
  (offsetof(DNBArchMachARM64::FPU, reg) +                                      \
   offsetof(DNBArchMachARM64::Context, vfp))
#define EXC_OFFSET(reg)                                                        \
  (offsetof(DNBArchMachARM64::EXC, reg) +                                      \
   offsetof(DNBArchMachARM64::Context, exc))

//#define FLOAT_FORMAT Float
#define DEFINE_VFP_V_IDX(idx)                                                  \
  {                                                                            \
    e_regSetVFP, vfp_v##idx, "v" #idx, "q" #idx, Vector, VectorOfUInt8, 16,    \
        VFP_V_OFFSET_IDX(idx), INVALID_NUB_REGNUM, dwarf_v##idx,               \
        INVALID_NUB_REGNUM, debugserver_vfp_v##idx, NULL, g_invalidate_v##idx  \
  }
#define DEFINE_PSEUDO_VFP_S_IDX(idx)                                           \
  {                                                                            \
    e_regSetVFP, vfp_s##idx, "s" #idx, NULL, IEEE754, Float, 4, 0,             \
        INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,            \
        INVALID_NUB_REGNUM, g_contained_v##idx, g_invalidate_v##idx            \
  }
#define DEFINE_PSEUDO_VFP_D_IDX(idx)                                           \
  {                                                                            \
    e_regSetVFP, vfp_d##idx, "d" #idx, NULL, IEEE754, Float, 8, 0,             \
        INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,            \
        INVALID_NUB_REGNUM, g_contained_v##idx, g_invalidate_v##idx            \
  }

// Floating point registers
const DNBRegisterInfo DNBArchMachARM64::g_vfp_registers[] = {
    DEFINE_VFP_V_IDX(0),
    DEFINE_VFP_V_IDX(1),
    DEFINE_VFP_V_IDX(2),
    DEFINE_VFP_V_IDX(3),
    DEFINE_VFP_V_IDX(4),
    DEFINE_VFP_V_IDX(5),
    DEFINE_VFP_V_IDX(6),
    DEFINE_VFP_V_IDX(7),
    DEFINE_VFP_V_IDX(8),
    DEFINE_VFP_V_IDX(9),
    DEFINE_VFP_V_IDX(10),
    DEFINE_VFP_V_IDX(11),
    DEFINE_VFP_V_IDX(12),
    DEFINE_VFP_V_IDX(13),
    DEFINE_VFP_V_IDX(14),
    DEFINE_VFP_V_IDX(15),
    DEFINE_VFP_V_IDX(16),
    DEFINE_VFP_V_IDX(17),
    DEFINE_VFP_V_IDX(18),
    DEFINE_VFP_V_IDX(19),
    DEFINE_VFP_V_IDX(20),
    DEFINE_VFP_V_IDX(21),
    DEFINE_VFP_V_IDX(22),
    DEFINE_VFP_V_IDX(23),
    DEFINE_VFP_V_IDX(24),
    DEFINE_VFP_V_IDX(25),
    DEFINE_VFP_V_IDX(26),
    DEFINE_VFP_V_IDX(27),
    DEFINE_VFP_V_IDX(28),
    DEFINE_VFP_V_IDX(29),
    DEFINE_VFP_V_IDX(30),
    DEFINE_VFP_V_IDX(31),
    {e_regSetVFP, vfp_fpsr, "fpsr", NULL, Uint, Hex, 4,
     VFP_V_OFFSET_IDX(32) + 0, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,
     INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, NULL, NULL},
    {e_regSetVFP, vfp_fpcr, "fpcr", NULL, Uint, Hex, 4,
     VFP_V_OFFSET_IDX(32) + 4, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,
     INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, NULL, NULL},

    DEFINE_PSEUDO_VFP_S_IDX(0),
    DEFINE_PSEUDO_VFP_S_IDX(1),
    DEFINE_PSEUDO_VFP_S_IDX(2),
    DEFINE_PSEUDO_VFP_S_IDX(3),
    DEFINE_PSEUDO_VFP_S_IDX(4),
    DEFINE_PSEUDO_VFP_S_IDX(5),
    DEFINE_PSEUDO_VFP_S_IDX(6),
    DEFINE_PSEUDO_VFP_S_IDX(7),
    DEFINE_PSEUDO_VFP_S_IDX(8),
    DEFINE_PSEUDO_VFP_S_IDX(9),
    DEFINE_PSEUDO_VFP_S_IDX(10),
    DEFINE_PSEUDO_VFP_S_IDX(11),
    DEFINE_PSEUDO_VFP_S_IDX(12),
    DEFINE_PSEUDO_VFP_S_IDX(13),
    DEFINE_PSEUDO_VFP_S_IDX(14),
    DEFINE_PSEUDO_VFP_S_IDX(15),
    DEFINE_PSEUDO_VFP_S_IDX(16),
    DEFINE_PSEUDO_VFP_S_IDX(17),
    DEFINE_PSEUDO_VFP_S_IDX(18),
    DEFINE_PSEUDO_VFP_S_IDX(19),
    DEFINE_PSEUDO_VFP_S_IDX(20),
    DEFINE_PSEUDO_VFP_S_IDX(21),
    DEFINE_PSEUDO_VFP_S_IDX(22),
    DEFINE_PSEUDO_VFP_S_IDX(23),
    DEFINE_PSEUDO_VFP_S_IDX(24),
    DEFINE_PSEUDO_VFP_S_IDX(25),
    DEFINE_PSEUDO_VFP_S_IDX(26),
    DEFINE_PSEUDO_VFP_S_IDX(27),
    DEFINE_PSEUDO_VFP_S_IDX(28),
    DEFINE_PSEUDO_VFP_S_IDX(29),
    DEFINE_PSEUDO_VFP_S_IDX(30),
    DEFINE_PSEUDO_VFP_S_IDX(31),

    DEFINE_PSEUDO_VFP_D_IDX(0),
    DEFINE_PSEUDO_VFP_D_IDX(1),
    DEFINE_PSEUDO_VFP_D_IDX(2),
    DEFINE_PSEUDO_VFP_D_IDX(3),
    DEFINE_PSEUDO_VFP_D_IDX(4),
    DEFINE_PSEUDO_VFP_D_IDX(5),
    DEFINE_PSEUDO_VFP_D_IDX(6),
    DEFINE_PSEUDO_VFP_D_IDX(7),
    DEFINE_PSEUDO_VFP_D_IDX(8),
    DEFINE_PSEUDO_VFP_D_IDX(9),
    DEFINE_PSEUDO_VFP_D_IDX(10),
    DEFINE_PSEUDO_VFP_D_IDX(11),
    DEFINE_PSEUDO_VFP_D_IDX(12),
    DEFINE_PSEUDO_VFP_D_IDX(13),
    DEFINE_PSEUDO_VFP_D_IDX(14),
    DEFINE_PSEUDO_VFP_D_IDX(15),
    DEFINE_PSEUDO_VFP_D_IDX(16),
    DEFINE_PSEUDO_VFP_D_IDX(17),
    DEFINE_PSEUDO_VFP_D_IDX(18),
    DEFINE_PSEUDO_VFP_D_IDX(19),
    DEFINE_PSEUDO_VFP_D_IDX(20),
    DEFINE_PSEUDO_VFP_D_IDX(21),
    DEFINE_PSEUDO_VFP_D_IDX(22),
    DEFINE_PSEUDO_VFP_D_IDX(23),
    DEFINE_PSEUDO_VFP_D_IDX(24),
    DEFINE_PSEUDO_VFP_D_IDX(25),
    DEFINE_PSEUDO_VFP_D_IDX(26),
    DEFINE_PSEUDO_VFP_D_IDX(27),
    DEFINE_PSEUDO_VFP_D_IDX(28),
    DEFINE_PSEUDO_VFP_D_IDX(29),
    DEFINE_PSEUDO_VFP_D_IDX(30),
    DEFINE_PSEUDO_VFP_D_IDX(31)

};

//_STRUCT_ARM_EXCEPTION_STATE64
//{
//	uint64_t	far; /* Virtual Fault Address */
//	uint32_t	esr; /* Exception syndrome */
//	uint32_t	exception; /* number of arm exception taken */
//};

// Exception registers
const DNBRegisterInfo DNBArchMachARM64::g_exc_registers[] = {
    {e_regSetEXC, exc_far, "far", NULL, Uint, Hex, 8, EXC_OFFSET(__far),
     INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,
     INVALID_NUB_REGNUM, NULL, NULL},
    {e_regSetEXC, exc_esr, "esr", NULL, Uint, Hex, 4, EXC_OFFSET(__esr),
     INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,
     INVALID_NUB_REGNUM, NULL, NULL},
    {e_regSetEXC, exc_exception, "exception", NULL, Uint, Hex, 4,
     EXC_OFFSET(__exception), INVALID_NUB_REGNUM, INVALID_NUB_REGNUM,
     INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, NULL, NULL}};

// Number of registers in each register set
const size_t DNBArchMachARM64::k_num_gpr_registers =
    sizeof(g_gpr_registers) / sizeof(DNBRegisterInfo);
const size_t DNBArchMachARM64::k_num_vfp_registers =
    sizeof(g_vfp_registers) / sizeof(DNBRegisterInfo);
const size_t DNBArchMachARM64::k_num_exc_registers =
    sizeof(g_exc_registers) / sizeof(DNBRegisterInfo);
const size_t DNBArchMachARM64::k_num_all_registers =
    k_num_gpr_registers + k_num_vfp_registers + k_num_exc_registers;

// Register set definitions. The first definitions at register set index
// of zero is for all registers, followed by other registers sets. The
// register information for the all register set need not be filled in.
const DNBRegisterSetInfo DNBArchMachARM64::g_reg_sets[] = {
    {"ARM64 Registers", NULL, k_num_all_registers},
    {"General Purpose Registers", g_gpr_registers, k_num_gpr_registers},
    {"Floating Point Registers", g_vfp_registers, k_num_vfp_registers},
    {"Exception State Registers", g_exc_registers, k_num_exc_registers}};
// Total number of register sets for this architecture
const size_t DNBArchMachARM64::k_num_register_sets =
    sizeof(g_reg_sets) / sizeof(DNBRegisterSetInfo);

const DNBRegisterSetInfo *
DNBArchMachARM64::GetRegisterSetInfo(nub_size_t *num_reg_sets) {
  *num_reg_sets = k_num_register_sets;
  return g_reg_sets;
}

bool DNBArchMachARM64::FixGenericRegisterNumber(uint32_t &set, uint32_t &reg) {
  if (set == REGISTER_SET_GENERIC) {
    switch (reg) {
    case GENERIC_REGNUM_PC: // Program Counter
      set = e_regSetGPR;
      reg = gpr_pc;
      break;

    case GENERIC_REGNUM_SP: // Stack Pointer
      set = e_regSetGPR;
      reg = gpr_sp;
      break;

    case GENERIC_REGNUM_FP: // Frame Pointer
      set = e_regSetGPR;
      reg = gpr_fp;
      break;

    case GENERIC_REGNUM_RA: // Return Address
      set = e_regSetGPR;
      reg = gpr_lr;
      break;

    case GENERIC_REGNUM_FLAGS: // Processor flags register
      set = e_regSetGPR;
      reg = gpr_cpsr;
      break;

    case GENERIC_REGNUM_ARG1:
    case GENERIC_REGNUM_ARG2:
    case GENERIC_REGNUM_ARG3:
    case GENERIC_REGNUM_ARG4:
    case GENERIC_REGNUM_ARG5:
    case GENERIC_REGNUM_ARG6:
      set = e_regSetGPR;
      reg = gpr_x0 + reg - GENERIC_REGNUM_ARG1;
      break;

    default:
      return false;
    }
  }
  return true;
}
bool DNBArchMachARM64::GetRegisterValue(uint32_t set, uint32_t reg,
                                        DNBRegisterValue *value) {
  if (!FixGenericRegisterNumber(set, reg))
    return false;

  if (GetRegisterState(set, false) != KERN_SUCCESS)
    return false;

  const DNBRegisterInfo *regInfo = m_thread->GetRegisterInfo(set, reg);
  if (regInfo) {
    value->info = *regInfo;
    switch (set) {
    case e_regSetGPR:
      if (reg <= gpr_pc) {
        switch (reg) {
#if defined(DEBUGSERVER_IS_ARM64E)
        case gpr_pc:
          value->value.uint64 = clear_pac_bits(
              reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_pc));
          break;
        case gpr_lr:
          value->value.uint64 = arm_thread_state64_get_lr(m_state.context.gpr);
          break;
        case gpr_sp:
          value->value.uint64 = clear_pac_bits(
              reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_sp));
          break;
        case gpr_fp:
          value->value.uint64 = clear_pac_bits(
              reinterpret_cast<uint64_t>(m_state.context.gpr.__opaque_fp));
          break;
#else
        case gpr_pc:
          value->value.uint64 = clear_pac_bits(m_state.context.gpr.__pc);
          break;
        case gpr_lr:
          value->value.uint64 = clear_pac_bits(m_state.context.gpr.__lr);
          break;
        case gpr_sp:
          value->value.uint64 = clear_pac_bits(m_state.context.gpr.__sp);
          break;
        case gpr_fp:
          value->value.uint64 = clear_pac_bits(m_state.context.gpr.__fp);
          break;
#endif
        default:
          value->value.uint64 = m_state.context.gpr.__x[reg];
        }
        return true;
      } else if (reg == gpr_cpsr) {
        value->value.uint32 = m_state.context.gpr.__cpsr;
        return true;
      }
      break;

    case e_regSetVFP:

      if (reg >= vfp_v0 && reg <= vfp_v31) {
#if defined(__arm64__) || defined(__aarch64__)
        memcpy(&value->value.v_uint8, &m_state.context.vfp.__v[reg - vfp_v0],
               16);
#else
        memcpy(&value->value.v_uint8,
               ((uint8_t *)&m_state.context.vfp.opaque) + ((reg - vfp_v0) * 16),
               16);
#endif
        return true;
      } else if (reg == vfp_fpsr) {
#if defined(__arm64__) || defined(__aarch64__)
        memcpy(&value->value.uint32, &m_state.context.vfp.__fpsr, 4);
#else
        memcpy(&value->value.uint32,
               ((uint8_t *)&m_state.context.vfp.opaque) + (32 * 16) + 0, 4);
#endif
        return true;
      } else if (reg == vfp_fpcr) {
#if defined(__arm64__) || defined(__aarch64__)
        memcpy(&value->value.uint32, &m_state.context.vfp.__fpcr, 4);
#else
        memcpy(&value->value.uint32,
               ((uint8_t *)&m_state.context.vfp.opaque) + (32 * 16) + 4, 4);
#endif
        return true;
      } else if (reg >= vfp_s0 && reg <= vfp_s31) {
#if defined(__arm64__) || defined(__aarch64__)
        memcpy(&value->value.v_uint8, &m_state.context.vfp.__v[reg - vfp_s0],
               4);
#else
        memcpy(&value->value.v_uint8,
               ((uint8_t *)&m_state.context.vfp.opaque) + ((reg - vfp_s0) * 16),
               4);
#endif
        return true;
      } else if (reg >= vfp_d0 && reg <= vfp_d31) {
#if defined(__arm64__) || defined(__aarch64__)
        memcpy(&value->value.v_uint8, &m_state.context.vfp.__v[reg - vfp_d0],
               8);
#else
        memcpy(&value->value.v_uint8,
               ((uint8_t *)&m_state.context.vfp.opaque) + ((reg - vfp_d0) * 16),
               8);
#endif
        return true;
      }
      break;

    case e_regSetEXC:
      if (reg == exc_far) {
        value->value.uint64 = m_state.context.exc.__far;
        return true;
      } else if (reg == exc_esr) {
        value->value.uint32 = m_state.context.exc.__esr;
        return true;
      } else if (reg == exc_exception) {
        value->value.uint32 = m_state.context.exc.__exception;
        return true;
      }
      break;
    }
  }
  return false;
}

bool DNBArchMachARM64::SetRegisterValue(uint32_t set, uint32_t reg,
                                        const DNBRegisterValue *value) {
  if (!FixGenericRegisterNumber(set, reg))
    return false;

  if (GetRegisterState(set, false) != KERN_SUCCESS)
    return false;

  bool success = false;
  const DNBRegisterInfo *regInfo = m_thread->GetRegisterInfo(set, reg);
  if (regInfo) {
    switch (set) {
    case e_regSetGPR:
      if (reg <= gpr_pc) {
#if defined(__LP64__)
          uint64_t signed_value = value->value.uint64;
#if __has_feature(ptrauth_calls)
          // The incoming value could be garbage.  Strip it to avoid
          // trapping when it gets resigned in the thread state.
          signed_value = (uint64_t) ptrauth_strip((void*) signed_value, ptrauth_key_function_pointer);
          signed_value = (uint64_t) ptrauth_sign_unauthenticated((void*) signed_value, ptrauth_key_function_pointer, 0);
#endif
        if (reg == gpr_pc)
         arm_thread_state64_set_pc_fptr (m_state.context.gpr, (void*) signed_value);
        else if (reg == gpr_lr)
          arm_thread_state64_set_lr_fptr (m_state.context.gpr, (void*) signed_value);
        else if (reg == gpr_sp)
          arm_thread_state64_set_sp (m_state.context.gpr, value->value.uint64);
        else if (reg == gpr_fp)
          arm_thread_state64_set_fp (m_state.context.gpr, value->value.uint64);
        else
          m_state.context.gpr.__x[reg] = value->value.uint64;
#else
        m_state.context.gpr.__x[reg] = value->value.uint64;
#endif
        success = true;
      } else if (reg == gpr_cpsr) {
        m_state.context.gpr.__cpsr = value->value.uint32;
        success = true;
      }
      break;

    case e_regSetVFP:
      if (reg >= vfp_v0 && reg <= vfp_v31) {
#if defined(__arm64__) || defined(__aarch64__)
        memcpy(&m_state.context.vfp.__v[reg - vfp_v0], &value->value.v_uint8,
               16);
#else
        memcpy(((uint8_t *)&m_state.context.vfp.opaque) + ((reg - vfp_v0) * 16),
               &value->value.v_uint8, 16);
#endif
        success = true;
      } else if (reg == vfp_fpsr) {
#if defined(__arm64__) || defined(__aarch64__)
        memcpy(&m_state.context.vfp.__fpsr, &value->value.uint32, 4);
#else
        memcpy(((uint8_t *)&m_state.context.vfp.opaque) + (32 * 16) + 0,
               &value->value.uint32, 4);
#endif
        success = true;
      } else if (reg == vfp_fpcr) {
#if defined(__arm64__) || defined(__aarch64__)
        memcpy(&m_state.context.vfp.__fpcr, &value->value.uint32, 4);
#else
        memcpy(((uint8_t *)m_state.context.vfp.opaque) + (32 * 16) + 4,
               &value->value.uint32, 4);
#endif
        success = true;
      } else if (reg >= vfp_s0 && reg <= vfp_s31) {
#if defined(__arm64__) || defined(__aarch64__)
        memcpy(&m_state.context.vfp.__v[reg - vfp_s0], &value->value.v_uint8,
               4);
#else
        memcpy(((uint8_t *)&m_state.context.vfp.opaque) + ((reg - vfp_s0) * 16),
               &value->value.v_uint8, 4);
#endif
        success = true;
      } else if (reg >= vfp_d0 && reg <= vfp_d31) {
#if defined(__arm64__) || defined(__aarch64__)
        memcpy(&m_state.context.vfp.__v[reg - vfp_d0], &value->value.v_uint8,
               8);
#else
        memcpy(((uint8_t *)&m_state.context.vfp.opaque) + ((reg - vfp_d0) * 16),
               &value->value.v_uint8, 8);
#endif
        success = true;
      }
      break;

    case e_regSetEXC:
      if (reg == exc_far) {
        m_state.context.exc.__far = value->value.uint64;
        success = true;
      } else if (reg == exc_esr) {
        m_state.context.exc.__esr = value->value.uint32;
        success = true;
      } else if (reg == exc_exception) {
        m_state.context.exc.__exception = value->value.uint32;
        success = true;
      }
      break;
    }
  }
  if (success)
    return SetRegisterState(set) == KERN_SUCCESS;
  return false;
}

kern_return_t DNBArchMachARM64::GetRegisterState(int set, bool force) {
  switch (set) {
  case e_regSetALL:
    return GetGPRState(force) | GetVFPState(force) | GetEXCState(force) |
           GetDBGState(force);
  case e_regSetGPR:
    return GetGPRState(force);
  case e_regSetVFP:
    return GetVFPState(force);
  case e_regSetEXC:
    return GetEXCState(force);
  case e_regSetDBG:
    return GetDBGState(force);
  default:
    break;
  }
  return KERN_INVALID_ARGUMENT;
}

kern_return_t DNBArchMachARM64::SetRegisterState(int set) {
  // Make sure we have a valid context to set.
  kern_return_t err = GetRegisterState(set, false);
  if (err != KERN_SUCCESS)
    return err;

  switch (set) {
  case e_regSetALL:
    return SetGPRState() | SetVFPState() | SetEXCState() | SetDBGState(false);
  case e_regSetGPR:
    return SetGPRState();
  case e_regSetVFP:
    return SetVFPState();
  case e_regSetEXC:
    return SetEXCState();
  case e_regSetDBG:
    return SetDBGState(false);
  default:
    break;
  }
  return KERN_INVALID_ARGUMENT;
}

bool DNBArchMachARM64::RegisterSetStateIsValid(int set) const {
  return m_state.RegsAreValid(set);
}

nub_size_t DNBArchMachARM64::GetRegisterContext(void *buf, nub_size_t buf_len) {
  nub_size_t size = sizeof(m_state.context.gpr) + sizeof(m_state.context.vfp) +
                    sizeof(m_state.context.exc);

  if (buf && buf_len) {
    if (size > buf_len)
      size = buf_len;

    bool force = false;
    if (GetGPRState(force) | GetVFPState(force) | GetEXCState(force))
      return 0;

    // Copy each struct individually to avoid any padding that might be between
    // the structs in m_state.context
    uint8_t *p = (uint8_t *)buf;
    ::memcpy(p, &m_state.context.gpr, sizeof(m_state.context.gpr));
    p += sizeof(m_state.context.gpr);
    ::memcpy(p, &m_state.context.vfp, sizeof(m_state.context.vfp));
    p += sizeof(m_state.context.vfp);
    ::memcpy(p, &m_state.context.exc, sizeof(m_state.context.exc));
    p += sizeof(m_state.context.exc);

    size_t bytes_written = p - (uint8_t *)buf;
    UNUSED_IF_ASSERT_DISABLED(bytes_written);
    assert(bytes_written == size);
  }
  DNBLogThreadedIf(
      LOG_THREAD,
      "DNBArchMachARM64::GetRegisterContext (buf = %p, len = %zu) => %zu", buf,
      buf_len, size);
  // Return the size of the register context even if NULL was passed in
  return size;
}

nub_size_t DNBArchMachARM64::SetRegisterContext(const void *buf,
                                                nub_size_t buf_len) {
  nub_size_t size = sizeof(m_state.context.gpr) + sizeof(m_state.context.vfp) +
                    sizeof(m_state.context.exc);

  if (buf == NULL || buf_len == 0)
    size = 0;

  if (size) {
    if (size > buf_len)
      size = buf_len;

    // Copy each struct individually to avoid any padding that might be between
    // the structs in m_state.context
    uint8_t *p = const_cast<uint8_t*>(reinterpret_cast<const uint8_t *>(buf));
    ::memcpy(&m_state.context.gpr, p, sizeof(m_state.context.gpr));
    p += sizeof(m_state.context.gpr);
    ::memcpy(&m_state.context.vfp, p, sizeof(m_state.context.vfp));
    p += sizeof(m_state.context.vfp);
    ::memcpy(&m_state.context.exc, p, sizeof(m_state.context.exc));
    p += sizeof(m_state.context.exc);

    size_t bytes_written = p - reinterpret_cast<const uint8_t *>(buf);
    UNUSED_IF_ASSERT_DISABLED(bytes_written);
    assert(bytes_written == size);
    SetGPRState();
    SetVFPState();
    SetEXCState();
  }
  DNBLogThreadedIf(
      LOG_THREAD,
      "DNBArchMachARM64::SetRegisterContext (buf = %p, len = %zu) => %zu", buf,
      buf_len, size);
  return size;
}

uint32_t DNBArchMachARM64::SaveRegisterState() {
  kern_return_t kret = ::thread_abort_safely(m_thread->MachPortNumber());
  DNBLogThreadedIf(
      LOG_THREAD, "thread = 0x%4.4x calling thread_abort_safely (tid) => %u "
                  "(SetGPRState() for stop_count = %u)",
      m_thread->MachPortNumber(), kret, m_thread->Process()->StopCount());

  // Always re-read the registers because above we call thread_abort_safely();
  bool force = true;

  if ((kret = GetGPRState(force)) != KERN_SUCCESS) {
    DNBLogThreadedIf(LOG_THREAD, "DNBArchMachARM64::SaveRegisterState () "
                                 "error: GPR regs failed to read: %u ",
                     kret);
  } else if ((kret = GetVFPState(force)) != KERN_SUCCESS) {
    DNBLogThreadedIf(LOG_THREAD, "DNBArchMachARM64::SaveRegisterState () "
                                 "error: %s regs failed to read: %u",
                     "VFP", kret);
  } else {
    const uint32_t save_id = GetNextRegisterStateSaveID();
    m_saved_register_states[save_id] = m_state.context;
    return save_id;
  }
  return UINT32_MAX;
}

bool DNBArchMachARM64::RestoreRegisterState(uint32_t save_id) {
  SaveRegisterStates::iterator pos = m_saved_register_states.find(save_id);
  if (pos != m_saved_register_states.end()) {
    m_state.context.gpr = pos->second.gpr;
    m_state.context.vfp = pos->second.vfp;
    kern_return_t kret;
    bool success = true;
    if ((kret = SetGPRState()) != KERN_SUCCESS) {
      DNBLogThreadedIf(LOG_THREAD, "DNBArchMachARM64::RestoreRegisterState "
                                   "(save_id = %u) error: GPR regs failed to "
                                   "write: %u",
                       save_id, kret);
      success = false;
    } else if ((kret = SetVFPState()) != KERN_SUCCESS) {
      DNBLogThreadedIf(LOG_THREAD, "DNBArchMachARM64::RestoreRegisterState "
                                   "(save_id = %u) error: %s regs failed to "
                                   "write: %u",
                       save_id, "VFP", kret);
      success = false;
    }
    m_saved_register_states.erase(pos);
    return success;
  }
  return false;
}

#endif // #if defined (ARM_THREAD_STATE64_COUNT)
#endif // #if defined (__arm__) || defined (__arm64__) || defined (__aarch64__)
