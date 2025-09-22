//===-- xray_AArch64.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Implementation of AArch64-specific routines (64-bit).
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_common.h"
#include "xray_defs.h"
#include "xray_interface_internal.h"
#include <atomic>
#include <cassert>

extern "C" void __clear_cache(void *start, void *end);

namespace __xray {

// The machine codes for some instructions used in runtime patching.
enum class PatchOpcodes : uint32_t {
  PO_StpX0X30SP_m16e = 0xA9BF7BE0, // STP X0, X30, [SP, #-16]!
  PO_LdrX16_12 = 0x58000070,       // LDR X16, #12
  PO_BlrX16 = 0xD63F0200,          // BLR X16
  PO_LdpX0X30SP_16 = 0xA8C17BE0,   // LDP X0, X30, [SP], #16
  PO_B32 = 0x14000008              // B #32
};

inline static bool patchSled(const bool Enable, const uint32_t FuncId,
                             const XRaySledEntry &Sled,
                             void (*TracingHook)()) XRAY_NEVER_INSTRUMENT {
  // When |Enable| == true,
  // We replace the following compile-time stub (sled):
  //
  // xray_sled_n:
  //   B #32
  //   7 NOPs (24 bytes)
  //
  // With the following runtime patch:
  //
  // xray_sled_n:
  //   STP X0, X30, [SP, #-16]! ; PUSH {r0, lr}
  //   LDR W17, #12 ; W17 := function ID
  //   LDR X16,#12 ; X16 := address of the trampoline
  //   BLR X16
  //   ;DATA: 32 bits of function ID
  //   ;DATA: lower 32 bits of the address of the trampoline
  //   ;DATA: higher 32 bits of the address of the trampoline
  //   LDP X0, X30, [SP], #16 ; POP {r0, lr}
  //
  // Replacement of the first 4-byte instruction should be the last and atomic
  // operation, so that the user code which reaches the sled concurrently
  // either jumps over the whole sled, or executes the whole sled when the
  // latter is ready.
  //
  // When |Enable|==false, we set back the first instruction in the sled to be
  //   B #32

  uint32_t *FirstAddress = reinterpret_cast<uint32_t *>(Sled.address());
  uint32_t *CurAddress = FirstAddress + 1;
  if (Enable) {
    *CurAddress++ = 0x18000071; // ldr w17, #12
    *CurAddress = uint32_t(PatchOpcodes::PO_LdrX16_12);
    CurAddress++;
    *CurAddress = uint32_t(PatchOpcodes::PO_BlrX16);
    CurAddress++;
    *CurAddress = FuncId;
    CurAddress++;
    *reinterpret_cast<void (**)()>(CurAddress) = TracingHook;
    CurAddress += 2;
    *CurAddress = uint32_t(PatchOpcodes::PO_LdpX0X30SP_16);
    CurAddress++;
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint32_t> *>(FirstAddress),
        uint32_t(PatchOpcodes::PO_StpX0X30SP_m16e), std::memory_order_release);
  } else {
    std::atomic_store_explicit(
        reinterpret_cast<std::atomic<uint32_t> *>(FirstAddress),
        uint32_t(PatchOpcodes::PO_B32), std::memory_order_release);
  }
  __clear_cache(reinterpret_cast<char *>(FirstAddress),
                reinterpret_cast<char *>(CurAddress));
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
  return patchSled(Enable, FuncId, Sled, __xray_FunctionTailExit);
}

// AArch64AsmPrinter::LowerPATCHABLE_EVENT_CALL generates this code sequence:
//
// .Lxray_event_sled_N:
//   b 1f
//   save x0 and x1 (and also x2 for TYPED_EVENT_CALL)
//   set up x0 and x1 (and also x2 for TYPED_EVENT_CALL)
//   bl __xray_CustomEvent or __xray_TypedEvent
//   restore x0 and x1 (and also x2 for TYPED_EVENT_CALL)
// 1f
//
// There are 6 instructions for EVENT_CALL and 9 for TYPED_EVENT_CALL.
//
// Enable: b .+24 => nop
// Disable: nop => b .+24
bool patchCustomEvent(const bool Enable, const uint32_t FuncId,
                      const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  uint32_t Inst = Enable ? 0xd503201f : 0x14000006;
  std::atomic_store_explicit(
      reinterpret_cast<std::atomic<uint32_t> *>(Sled.address()), Inst,
      std::memory_order_release);
  return false;
}

// Enable: b +36 => nop
// Disable: nop => b +36
bool patchTypedEvent(const bool Enable, const uint32_t FuncId,
                     const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  uint32_t Inst = Enable ? 0xd503201f : 0x14000009;
  std::atomic_store_explicit(
      reinterpret_cast<std::atomic<uint32_t> *>(Sled.address()), Inst,
      std::memory_order_release);
  return false;
}

// FIXME: Maybe implement this better?
bool probeRequiredCPUFeatures() XRAY_NEVER_INSTRUMENT { return true; }

} // namespace __xray
