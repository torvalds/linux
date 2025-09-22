//===-------- xray_loongarch64.cpp ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Implementation of loongarch-specific routines.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_common.h"
#include "xray_defs.h"
#include "xray_interface_internal.h"
#include <atomic>

namespace __xray {

enum RegNum : uint32_t {
  RN_RA = 1,
  RN_SP = 3,
  RN_T0 = 12,
  RN_T1 = 13,
};

// Encode instructions in the 2RIx format, where the primary formats here
// are 2RI12-type and 2RI16-type.
static inline uint32_t
encodeInstruction2RIx(uint32_t Opcode, uint32_t Rd, uint32_t Rj,
                      uint32_t Imm) XRAY_NEVER_INSTRUMENT {
  return Opcode | (Imm << 10) | (Rj << 5) | Rd;
}

// Encode instructions in 1RI20 format, e.g. lu12i.w/lu32i.d.
static inline uint32_t
encodeInstruction1RI20(uint32_t Opcode, uint32_t Rd,
                       uint32_t Imm) XRAY_NEVER_INSTRUMENT {
  return Opcode | (Imm << 5) | Rd;
}

static inline bool patchSled(const bool Enable, const uint32_t FuncId,
                             const XRaySledEntry &Sled,
                             void (*TracingHook)()) XRAY_NEVER_INSTRUMENT {
  // When |Enable| == true,
  // We replace the following compile-time stub (sled):
  //
  // .Lxray_sled_beginN:
  //	B .Lxray_sled_endN
  //	11 NOPs (44 bytes)
  // .Lxray_sled_endN:
  //
  // With the following runtime patch:
  //
  // xray_sled_n:
  //   addi.d  sp, sp, -16                       ; create the stack frame
  //   st.d    ra, sp, 8                         ; save the return address
  //   lu12i.w t0, %abs_hi20(__xray_FunctionEntry/Exit)
  //   ori     t0, t0, %abs_lo12(__xray_FunctionEntry/Exit)
  //   lu32i.d t0, %abs64_lo20(__xray_FunctionEntry/Exit)
  //   lu52i.d t0, t0, %abs64_hi12(__xray_FunctionEntry/Exit)
  //   lu12i.w t1, %abs_hi20(function_id)
  //   ori     t1, t1, %abs_lo12(function_id)    ; pass the function id
  //   jirl    ra, t0, 0                         ; call the tracing hook
  //   ld.d    ra, sp, 8                         ; restore the return address
  //   addi.d  sp, sp, 16                        ; de-allocate the stack frame
  //
  // Replacement of the first 4-byte instruction should be the last and atomic
  // operation, so that the user code which reaches the sled concurrently
  // either jumps over the whole sled, or executes the whole sled when the
  // latter is ready.
  //
  // When |Enable|==false, we set the first instruction in the sled back to
  //   B #48

  uint32_t *Address = reinterpret_cast<uint32_t *>(Sled.address());
  if (Enable) {
    uint32_t LoTracingHookAddr = reinterpret_cast<int64_t>(TracingHook) & 0xfff;
    uint32_t HiTracingHookAddr =
        (reinterpret_cast<int64_t>(TracingHook) >> 12) & 0xfffff;
    uint32_t HigherTracingHookAddr =
        (reinterpret_cast<int64_t>(TracingHook) >> 32) & 0xfffff;
    uint32_t HighestTracingHookAddr =
        (reinterpret_cast<int64_t>(TracingHook) >> 52) & 0xfff;
    uint32_t LoFunctionID = FuncId & 0xfff;
    uint32_t HiFunctionID = (FuncId >> 12) & 0xfffff;
    Address[1] = encodeInstruction2RIx(0x29c00000, RegNum::RN_RA, RegNum::RN_SP,
                                       0x8); // st.d ra, sp, 8
    Address[2] = encodeInstruction1RI20(
        0x14000000, RegNum::RN_T0,
        HiTracingHookAddr); // lu12i.w t0, HiTracingHookAddr
    Address[3] = encodeInstruction2RIx(
        0x03800000, RegNum::RN_T0, RegNum::RN_T0,
        LoTracingHookAddr); // ori t0, t0, LoTracingHookAddr
    Address[4] = encodeInstruction1RI20(
        0x16000000, RegNum::RN_T0,
        HigherTracingHookAddr); // lu32i.d t0, HigherTracingHookAddr
    Address[5] = encodeInstruction2RIx(
        0x03000000, RegNum::RN_T0, RegNum::RN_T0,
        HighestTracingHookAddr); // lu52i.d t0, t0, HighestTracingHookAddr
    Address[6] =
        encodeInstruction1RI20(0x14000000, RegNum::RN_T1,
                               HiFunctionID); // lu12i.w t1, HiFunctionID
    Address[7] =
        encodeInstruction2RIx(0x03800000, RegNum::RN_T1, RegNum::RN_T1,
                              LoFunctionID); // ori t1, t1, LoFunctionID
    Address[8] = encodeInstruction2RIx(0x4c000000, RegNum::RN_RA, RegNum::RN_T0,
                                       0); // jirl ra, t0, 0
    Address[9] = encodeInstruction2RIx(0x28c00000, RegNum::RN_RA, RegNum::RN_SP,
                                       0x8); // ld.d ra, sp, 8
    Address[10] = encodeInstruction2RIx(
        0x02c00000, RegNum::RN_SP, RegNum::RN_SP, 0x10); // addi.d sp, sp, 16
    uint32_t CreateStackSpace = encodeInstruction2RIx(
        0x02c00000, RegNum::RN_SP, RegNum::RN_SP, 0xff0); // addi.d sp, sp, -16
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint32_t> *>(Address), CreateStackSpace,
        std::memory_order_release);
  } else {
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint32_t> *>(Address),
        uint32_t(0x50003000), std::memory_order_release); // b #48
  }
  return true;
}

bool patchFunctionEntry(const bool Enable, const uint32_t FuncId,
                        const XRaySledEntry &Sled,
                        void (*Trampoline)()) XRAY_NEVER_INSTRUMENT {
  return patchSled(Enable, FuncId, Sled, Trampoline);
}

bool patchFunctionExit(const bool Enable, const uint32_t FuncId,
                       const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  return patchSled(Enable, FuncId, Sled, __xray_FunctionExit);
}

bool patchFunctionTailExit(const bool Enable, const uint32_t FuncId,
                           const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // TODO: In the future we'd need to distinguish between non-tail exits and
  // tail exits for better information preservation.
  return patchSled(Enable, FuncId, Sled, __xray_FunctionExit);
}

bool patchCustomEvent(const bool Enable, const uint32_t FuncId,
                      const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // FIXME: Implement in loongarch?
  return false;
}

bool patchTypedEvent(const bool Enable, const uint32_t FuncId,
                     const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // FIXME: Implement in loongarch?
  return false;
}
} // namespace __xray

extern "C" void __xray_ArgLoggerEntry() XRAY_NEVER_INSTRUMENT {
  // TODO: This will have to be implemented in the trampoline assembly file.
}
