//===-- xray_mips64.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Implementation of MIPS64-specific routines.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_common.h"
#include "xray_defs.h"
#include "xray_interface_internal.h"
#include <atomic>

namespace __xray {

// The machine codes for some instructions used in runtime patching.
enum PatchOpcodes : uint32_t {
  PO_DADDIU = 0x64000000, // daddiu rt, rs, imm
  PO_SD = 0xFC000000,     // sd rt, base(offset)
  PO_LUI = 0x3C000000,    // lui rt, imm
  PO_ORI = 0x34000000,    // ori rt, rs, imm
  PO_DSLL = 0x00000038,   // dsll rd, rt, sa
  PO_JALR = 0x00000009,   // jalr rs
  PO_LD = 0xDC000000,     // ld rt, base(offset)
  PO_B60 = 0x1000000f,    // b #60
  PO_NOP = 0x0,           // nop
};

enum RegNum : uint32_t {
  RN_T0 = 0xC,
  RN_T9 = 0x19,
  RN_RA = 0x1F,
  RN_SP = 0x1D,
};

inline static uint32_t encodeInstruction(uint32_t Opcode, uint32_t Rs,
                                         uint32_t Rt,
                                         uint32_t Imm) XRAY_NEVER_INSTRUMENT {
  return (Opcode | Rs << 21 | Rt << 16 | Imm);
}

inline static uint32_t
encodeSpecialInstruction(uint32_t Opcode, uint32_t Rs, uint32_t Rt, uint32_t Rd,
                         uint32_t Imm) XRAY_NEVER_INSTRUMENT {
  return (Rs << 21 | Rt << 16 | Rd << 11 | Imm << 6 | Opcode);
}

inline static bool patchSled(const bool Enable, const uint32_t FuncId,
                             const XRaySledEntry &Sled,
                             void (*TracingHook)()) XRAY_NEVER_INSTRUMENT {
  // When |Enable| == true,
  // We replace the following compile-time stub (sled):
  //
  // xray_sled_n:
  //	B .tmpN
  //	15 NOPs (60 bytes)
  //	.tmpN
  //
  // With the following runtime patch:
  //
  // xray_sled_n (64-bit):
  //    daddiu sp, sp, -16                      ;create stack frame
  //    nop
  //    sd ra, 8(sp)                            ;save return address
  //    sd t9, 0(sp)                            ;save register t9
  //    lui t9, %highest(__xray_FunctionEntry/Exit)
  //    ori t9, t9, %higher(__xray_FunctionEntry/Exit)
  //    dsll t9, t9, 16
  //    ori t9, t9, %hi(__xray_FunctionEntry/Exit)
  //    dsll t9, t9, 16
  //    ori t9, t9, %lo(__xray_FunctionEntry/Exit)
  //    lui t0, %hi(function_id)
  //    jalr t9                                 ;call Tracing hook
  //    ori t0, t0, %lo(function_id)            ;pass function id (delay slot)
  //    ld t9, 0(sp)                            ;restore register t9
  //    ld ra, 8(sp)                            ;restore return address
  //    daddiu sp, sp, 16                       ;delete stack frame
  //
  // Replacement of the first 4-byte instruction should be the last and atomic
  // operation, so that the user code which reaches the sled concurrently
  // either jumps over the whole sled, or executes the whole sled when the
  // latter is ready.
  //
  // When |Enable|==false, we set back the first instruction in the sled to be
  //   B #60

  uint32_t *Address = reinterpret_cast<uint32_t *>(Sled.address());
  if (Enable) {
    uint32_t LoTracingHookAddr =
        reinterpret_cast<int64_t>(TracingHook) & 0xffff;
    uint32_t HiTracingHookAddr =
        (reinterpret_cast<int64_t>(TracingHook) >> 16) & 0xffff;
    uint32_t HigherTracingHookAddr =
        (reinterpret_cast<int64_t>(TracingHook) >> 32) & 0xffff;
    uint32_t HighestTracingHookAddr =
        (reinterpret_cast<int64_t>(TracingHook) >> 48) & 0xffff;
    uint32_t LoFunctionID = FuncId & 0xffff;
    uint32_t HiFunctionID = (FuncId >> 16) & 0xffff;
    Address[2] = encodeInstruction(PatchOpcodes::PO_SD, RegNum::RN_SP,
                                   RegNum::RN_RA, 0x8);
    Address[3] = encodeInstruction(PatchOpcodes::PO_SD, RegNum::RN_SP,
                                   RegNum::RN_T9, 0x0);
    Address[4] = encodeInstruction(PatchOpcodes::PO_LUI, 0x0, RegNum::RN_T9,
                                   HighestTracingHookAddr);
    Address[5] = encodeInstruction(PatchOpcodes::PO_ORI, RegNum::RN_T9,
                                   RegNum::RN_T9, HigherTracingHookAddr);
    Address[6] = encodeSpecialInstruction(PatchOpcodes::PO_DSLL, 0x0,
                                          RegNum::RN_T9, RegNum::RN_T9, 0x10);
    Address[7] = encodeInstruction(PatchOpcodes::PO_ORI, RegNum::RN_T9,
                                   RegNum::RN_T9, HiTracingHookAddr);
    Address[8] = encodeSpecialInstruction(PatchOpcodes::PO_DSLL, 0x0,
                                          RegNum::RN_T9, RegNum::RN_T9, 0x10);
    Address[9] = encodeInstruction(PatchOpcodes::PO_ORI, RegNum::RN_T9,
                                   RegNum::RN_T9, LoTracingHookAddr);
    Address[10] = encodeInstruction(PatchOpcodes::PO_LUI, 0x0, RegNum::RN_T0,
                                    HiFunctionID);
    Address[11] = encodeSpecialInstruction(PatchOpcodes::PO_JALR, RegNum::RN_T9,
                                           0x0, RegNum::RN_RA, 0X0);
    Address[12] = encodeInstruction(PatchOpcodes::PO_ORI, RegNum::RN_T0,
                                    RegNum::RN_T0, LoFunctionID);
    Address[13] = encodeInstruction(PatchOpcodes::PO_LD, RegNum::RN_SP,
                                    RegNum::RN_T9, 0x0);
    Address[14] = encodeInstruction(PatchOpcodes::PO_LD, RegNum::RN_SP,
                                    RegNum::RN_RA, 0x8);
    Address[15] = encodeInstruction(PatchOpcodes::PO_DADDIU, RegNum::RN_SP,
                                    RegNum::RN_SP, 0x10);
    uint32_t CreateStackSpace = encodeInstruction(
        PatchOpcodes::PO_DADDIU, RegNum::RN_SP, RegNum::RN_SP, 0xfff0);
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint32_t> *>(Address), CreateStackSpace,
        std::memory_order_release);
  } else {
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint32_t> *>(Address),
        uint32_t(PatchOpcodes::PO_B60), std::memory_order_release);
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
  // FIXME: In the future we'd need to distinguish between non-tail exits and
  // tail exits for better information preservation.
  return patchSled(Enable, FuncId, Sled, __xray_FunctionExit);
}

bool patchCustomEvent(const bool Enable, const uint32_t FuncId,
                      const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // FIXME: Implement in mips64?
  return false;
}

bool patchTypedEvent(const bool Enable, const uint32_t FuncId,
                     const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // FIXME: Implement in mips64?
  return false;
}
} // namespace __xray

extern "C" void __xray_ArgLoggerEntry() XRAY_NEVER_INSTRUMENT {
  // FIXME: this will have to be implemented in the trampoline assembly file
}
