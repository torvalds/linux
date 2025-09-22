//===-- xray_hexagon.cpp --------------------------------------*- C++ ---*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Implementation of hexagon-specific routines (32-bit).
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_common.h"
#include "xray_defs.h"
#include "xray_interface_internal.h"
#include <assert.h>
#include <atomic>

namespace __xray {

// The machine codes for some instructions used in runtime patching.
enum PatchOpcodes : uint32_t {
  PO_JUMPI_14 = 0x5800c00a, // jump #0x014 (PC + 0x014)
  PO_CALLR_R6 = 0x50a6c000, // indirect call: callr r6
  PO_TFR_IMM = 0x78000000,  // transfer immed
                            // ICLASS 0x7 - S2-type A-type
  PO_IMMEXT = 0x00000000, // constant extender
};

enum PacketWordParseBits : uint32_t {
  PP_DUPLEX = 0x00 << 14,
  PP_NOT_END = 0x01 << 14,
  PP_PACKET_END = 0x03 << 14,
};

enum RegNum : uint32_t {
  RN_R6 = 0x6,
  RN_R7 = 0x7,
};

inline static uint32_t
encodeExtendedTransferImmediate(uint32_t Imm, RegNum DestReg,
                                bool PacketEnd = false) XRAY_NEVER_INSTRUMENT {
  static const uint32_t REG_MASK = 0x1f;
  assert((DestReg & (~REG_MASK)) == 0);
  // The constant-extended register transfer encodes the 6 least
  // significant bits of the effective constant:
  Imm = Imm & 0x03f;
  const PacketWordParseBits ParseBits = PacketEnd ? PP_PACKET_END : PP_NOT_END;

  return PO_TFR_IMM | ParseBits | (Imm << 5) | (DestReg & REG_MASK);
}

inline static uint32_t
encodeConstantExtender(uint32_t Imm) XRAY_NEVER_INSTRUMENT {
  // Bits   Name      Description
  // -----  -------   ------------------------------------------
  // 31:28  ICLASS    Instruction class = 0000
  // 27:16  high      High 12 bits of 26-bit constant extension
  // 15:14  Parse     Parse bits
  // 13:0   low       Low 14 bits of 26-bit constant extension
  static const uint32_t IMM_MASK_LOW = 0x03fff;
  static const uint32_t IMM_MASK_HIGH = 0x00fff << 14;

  // The extender encodes the 26 most significant bits of the effective
  // constant:
  Imm = Imm >> 6;

  const uint32_t high = (Imm & IMM_MASK_HIGH) << 16;
  const uint32_t low = Imm & IMM_MASK_LOW;

  return PO_IMMEXT | high | PP_NOT_END | low;
}

static void WriteInstFlushCache(void *Addr, uint32_t NewInstruction) {
  asm volatile("icinva(%[inst_addr])\n\t"
               "isync\n\t"
               "memw(%[inst_addr]) = %[new_inst]\n\t"
               "dccleaninva(%[inst_addr])\n\t"
               "syncht\n\t"
               :
               : [ inst_addr ] "r"(Addr), [ new_inst ] "r"(NewInstruction)
               : "memory");
}

inline static bool patchSled(const bool Enable, const uint32_t FuncId,
                             const XRaySledEntry &Sled,
                             void (*TracingHook)()) XRAY_NEVER_INSTRUMENT {
  // When |Enable| == true,
  // We replace the following compile-time stub (sled):
  //
  // .L_xray_sled_N:
  // <xray_sled_base>:
  // {  jump .Ltmp0 }
  // {  nop
  //    nop
  //    nop
  //    nop }
  // .Ltmp0:

  // With the following runtime patch:
  //
  // xray_sled_n (32-bit):
  //
  // <xray_sled_n>:
  // {  immext(#...) // upper 26-bits of func id
  //    r7 = ##...   // lower  6-bits of func id
  //    immext(#...) // upper 26-bits of trampoline
  //    r6 = ##... }  // lower 6 bits of trampoline
  // {  callr r6 }
  //
  // When |Enable|==false, we set back the first instruction in the sled to be
  // {  jump .Ltmp0 }

  uint32_t *FirstAddress = reinterpret_cast<uint32_t *>(Sled.address());
  if (Enable) {
    uint32_t *CurAddress = FirstAddress + 1;
    *CurAddress = encodeExtendedTransferImmediate(FuncId, RN_R7);
    CurAddress++;
    *CurAddress = encodeConstantExtender(reinterpret_cast<uint32_t>(TracingHook));
    CurAddress++;
    *CurAddress =
        encodeExtendedTransferImmediate(reinterpret_cast<uint32_t>(TracingHook), RN_R6, true);
    CurAddress++;

    *CurAddress = uint32_t(PO_CALLR_R6);

    WriteInstFlushCache(FirstAddress, uint32_t(encodeConstantExtender(FuncId)));
  } else {
    WriteInstFlushCache(FirstAddress, uint32_t(PatchOpcodes::PO_JUMPI_14));
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
  return patchSled(Enable, FuncId, Sled, __xray_FunctionExit);
}

bool patchCustomEvent(const bool Enable, const uint32_t FuncId,
                      const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // FIXME: Implement in hexagon?
  return false;
}

bool patchTypedEvent(const bool Enable, const uint32_t FuncId,
                     const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // FIXME: Implement in hexagon?
  return false;
}

} // namespace __xray

extern "C" void __xray_ArgLoggerEntry() XRAY_NEVER_INSTRUMENT {
  // FIXME: this will have to be implemented in the trampoline assembly file
}
