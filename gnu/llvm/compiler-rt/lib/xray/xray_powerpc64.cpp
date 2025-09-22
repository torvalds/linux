//===-- xray_powerpc64.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Implementation of powerpc64 and powerpc64le routines.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_common.h"
#include "xray_defs.h"
#include "xray_interface_internal.h"
#include "xray_utils.h"
#include <atomic>
#include <cassert>
#include <cstring>

#ifndef __LITTLE_ENDIAN__
#error powerpc64 big endian is not supported for now.
#endif

namespace {

constexpr unsigned long long JumpOverInstNum = 7;

void clearCache(void *Addr, size_t Len) {
  const size_t LineSize = 32;

  const intptr_t Mask = ~(LineSize - 1);
  const intptr_t StartLine = ((intptr_t)Addr) & Mask;
  const intptr_t EndLine = ((intptr_t)Addr + Len + LineSize - 1) & Mask;

  for (intptr_t Line = StartLine; Line < EndLine; Line += LineSize)
    asm volatile("dcbf 0, %0" : : "r"(Line));
  asm volatile("sync");

  for (intptr_t Line = StartLine; Line < EndLine; Line += LineSize)
    asm volatile("icbi 0, %0" : : "r"(Line));
  asm volatile("isync");
}

} // namespace

extern "C" void __clear_cache(void *start, void *end);

namespace __xray {

bool patchFunctionEntry(const bool Enable, uint32_t FuncId,
                        const XRaySledEntry &Sled,
                        void (*Trampoline)()) XRAY_NEVER_INSTRUMENT {
  const uint64_t Address = Sled.address();
  if (Enable) {
    // lis 0, FuncId[16..32]
    // li 0, FuncId[0..15]
    *reinterpret_cast<uint64_t *>(Address) =
        (0x3c000000ull + (FuncId >> 16)) +
        ((0x60000000ull + (FuncId & 0xffff)) << 32);
  } else {
    // b +JumpOverInstNum instructions.
    *reinterpret_cast<uint32_t *>(Address) =
        0x48000000ull + (JumpOverInstNum << 2);
  }
  clearCache(reinterpret_cast<void *>(Address), 8);
  return true;
}

bool patchFunctionExit(const bool Enable, uint32_t FuncId,
                       const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  const uint64_t Address = Sled.address();
  if (Enable) {
    // lis 0, FuncId[16..32]
    // li 0, FuncId[0..15]
    *reinterpret_cast<uint64_t *>(Address) =
        (0x3c000000ull + (FuncId >> 16)) +
        ((0x60000000ull + (FuncId & 0xffff)) << 32);
  } else {
    // Copy the blr/b instruction after JumpOverInstNum instructions.
    *reinterpret_cast<uint32_t *>(Address) =
        *(reinterpret_cast<uint32_t *>(Address) + JumpOverInstNum);
  }
  clearCache(reinterpret_cast<void *>(Address), 8);
  return true;
}

bool patchFunctionTailExit(const bool Enable, const uint32_t FuncId,
                           const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  return patchFunctionExit(Enable, FuncId, Sled);
}

// FIXME: Maybe implement this better?
bool probeRequiredCPUFeatures() XRAY_NEVER_INSTRUMENT { return true; }

bool patchCustomEvent(const bool Enable, const uint32_t FuncId,
                      const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // FIXME: Implement in powerpc64?
  return false;
}

bool patchTypedEvent(const bool Enable, const uint32_t FuncId,
                     const XRaySledEntry &Sled) XRAY_NEVER_INSTRUMENT {
  // FIXME: Implement in powerpc64?
  return false;
}

} // namespace __xray

extern "C" void __xray_ArgLoggerEntry() XRAY_NEVER_INSTRUMENT {
  // FIXME: this will have to be implemented in the trampoline assembly file
}
