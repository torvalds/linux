//===-- hwasan_checks.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//

#ifndef HWASAN_CHECKS_H
#define HWASAN_CHECKS_H

#include "hwasan_allocator.h"
#include "hwasan_mapping.h"
#include "hwasan_registers.h"
#include "sanitizer_common/sanitizer_common.h"

namespace __hwasan {

enum class ErrorAction { Abort, Recover };
enum class AccessType { Load, Store };

// Used when the access size is known.
constexpr unsigned SigTrapEncoding(ErrorAction EA, AccessType AT,
                                   unsigned LogSize) {
  return 0x20 * (EA == ErrorAction::Recover) +
         0x10 * (AT == AccessType::Store) + LogSize;
}

// Used when the access size varies at runtime.
constexpr unsigned SigTrapEncoding(ErrorAction EA, AccessType AT) {
  return SigTrapEncoding(EA, AT, 0xf);
}

template <ErrorAction EA, AccessType AT, size_t LogSize>
__attribute__((always_inline)) static void SigTrap(uptr p) {
  // Other platforms like linux can use signals for intercepting an exception
  // and dispatching to HandleTagMismatch. The fuchsias implementation doesn't
  // use signals so we can call it here directly instead.
#if CAN_GET_REGISTERS && SANITIZER_FUCHSIA
  auto regs = GetRegisters();
  size_t size = 2 << LogSize;
  AccessInfo access_info = {
      .addr = p,
      .size = size,
      .is_store = AT == AccessType::Store,
      .is_load = AT == AccessType::Load,
      .recover = EA == ErrorAction::Recover,
  };
  HandleTagMismatch(access_info, (uptr)__builtin_return_address(0),
                    (uptr)__builtin_frame_address(0), /*uc=*/nullptr, regs.x);
#elif defined(__aarch64__)
  (void)p;
  // 0x900 is added to do not interfere with the kernel use of lower values of
  // brk immediate.
  register uptr x0 asm("x0") = p;
  asm("brk %1\n\t" ::"r"(x0), "n"(0x900 + SigTrapEncoding(EA, AT, LogSize)));
#elif defined(__x86_64__)
  // INT3 + NOP DWORD ptr [EAX + X] to pass X to our signal handler, 5 bytes
  // total. The pointer is passed via rdi.
  // 0x40 is added as a safeguard, to help distinguish our trap from others and
  // to avoid 0 offsets in the command (otherwise it'll be reduced to a
  // different nop command, the three bytes one).
  asm volatile(
      "int3\n"
      "nopl %c0(%%rax)\n" ::"n"(0x40 + SigTrapEncoding(EA, AT, LogSize)),
      "D"(p));
#elif SANITIZER_RISCV64
  // Put pointer into x10
  // addiw contains immediate of 0x40 + X, where 0x40 is magic number and X
  // encodes access size
  register uptr x10 asm("x10") = p;
  asm volatile(
      "ebreak\n"
      "addiw x0, x0, %1\n" ::"r"(x10),
      "I"(0x40 + SigTrapEncoding(EA, AT, LogSize)));
#else
  // FIXME: not always sigill.
  __builtin_trap();
#endif
  // __builtin_unreachable();
}

// Version with access size which is not power of 2
template <ErrorAction EA, AccessType AT>
__attribute__((always_inline)) static void SigTrap(uptr p, uptr size) {
  // Other platforms like linux can use signals for intercepting an exception
  // and dispatching to HandleTagMismatch. The fuchsias implementation doesn't
  // use signals so we can call it here directly instead.
#if CAN_GET_REGISTERS && SANITIZER_FUCHSIA
  auto regs = GetRegisters();
  AccessInfo access_info = {
      .addr = p,
      .size = size,
      .is_store = AT == AccessType::Store,
      .is_load = AT == AccessType::Load,
      .recover = EA == ErrorAction::Recover,
  };
  HandleTagMismatch(access_info, (uptr)__builtin_return_address(0),
                    (uptr)__builtin_frame_address(0), /*uc=*/nullptr, regs.x);
#elif defined(__aarch64__)
  register uptr x0 asm("x0") = p;
  register uptr x1 asm("x1") = size;
  asm("brk %2\n\t" ::"r"(x0), "r"(x1), "n"(0x900 + SigTrapEncoding(EA, AT)));
#elif defined(__x86_64__)
  // Size is stored in rsi.
  asm volatile(
      "int3\n"
      "nopl %c0(%%rax)\n" ::"n"(0x40 + SigTrapEncoding(EA, AT)),
      "D"(p), "S"(size));
#elif SANITIZER_RISCV64
  // Put access size into x11
  register uptr x10 asm("x10") = p;
  register uptr x11 asm("x11") = size;
  asm volatile(
      "ebreak\n"
      "addiw x0, x0, %2\n" ::"r"(x10),
      "r"(x11), "I"(0x40 + SigTrapEncoding(EA, AT)));
#else
  __builtin_trap();
#endif
  // __builtin_unreachable();
}

__attribute__((always_inline, nodebug)) static inline uptr ShortTagSize(
    tag_t mem_tag, uptr ptr) {
  DCHECK(IsAligned(ptr, kShadowAlignment));
  tag_t ptr_tag = GetTagFromPointer(ptr);
  if (ptr_tag == mem_tag)
    return kShadowAlignment;
  if (!mem_tag || mem_tag >= kShadowAlignment)
    return 0;
  if (*(u8 *)(ptr | (kShadowAlignment - 1)) != ptr_tag)
    return 0;
  return mem_tag;
}

__attribute__((always_inline, nodebug)) static inline bool
PossiblyShortTagMatches(tag_t mem_tag, uptr ptr, uptr sz) {
  tag_t ptr_tag = GetTagFromPointer(ptr);
  if (ptr_tag == mem_tag)
    return true;
  if (mem_tag >= kShadowAlignment)
    return false;
  if ((ptr & (kShadowAlignment - 1)) + sz > mem_tag)
    return false;
  return *(u8 *)(ptr | (kShadowAlignment - 1)) == ptr_tag;
}

template <ErrorAction EA, AccessType AT, unsigned LogSize>
__attribute__((always_inline, nodebug)) static void CheckAddress(uptr p) {
  if (!InTaggableRegion(p))
    return;
  uptr ptr_raw = p & ~kAddressTagMask;
  tag_t mem_tag = *(tag_t *)MemToShadow(ptr_raw);
  if (UNLIKELY(!PossiblyShortTagMatches(mem_tag, p, 1 << LogSize))) {
    SigTrap<EA, AT, LogSize>(p);
    if (EA == ErrorAction::Abort)
      __builtin_unreachable();
  }
}

template <ErrorAction EA, AccessType AT>
__attribute__((always_inline, nodebug)) static void CheckAddressSized(uptr p,
                                                                      uptr sz) {
  if (sz == 0 || !InTaggableRegion(p))
    return;
  tag_t ptr_tag = GetTagFromPointer(p);
  uptr ptr_raw = p & ~kAddressTagMask;
  tag_t *shadow_first = (tag_t *)MemToShadow(ptr_raw);
  tag_t *shadow_last = (tag_t *)MemToShadow(ptr_raw + sz);
  for (tag_t *t = shadow_first; t < shadow_last; ++t)
    if (UNLIKELY(ptr_tag != *t)) {
      SigTrap<EA, AT>(p, sz);
      if (EA == ErrorAction::Abort)
        __builtin_unreachable();
    }
  uptr end = p + sz;
  uptr tail_sz = end & (kShadowAlignment - 1);
  if (UNLIKELY(tail_sz != 0 &&
               !PossiblyShortTagMatches(
                   *shadow_last, end & ~(kShadowAlignment - 1), tail_sz))) {
    SigTrap<EA, AT>(p, sz);
    if (EA == ErrorAction::Abort)
      __builtin_unreachable();
  }
}

}  // end namespace __hwasan

#endif  // HWASAN_CHECKS_H
