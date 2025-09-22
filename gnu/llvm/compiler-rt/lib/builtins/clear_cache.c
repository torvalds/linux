//===-- clear_cache.c - Implement __clear_cache ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"
#if defined(__linux__)
#include <assert.h>
#endif
#include <stddef.h>

#if __APPLE__
#include <libkern/OSCacheControl.h>
#endif

#if defined(_WIN32)
// Forward declare Win32 APIs since the GCC mode driver does not handle the
// newer SDKs as well as needed.
uint32_t FlushInstructionCache(uintptr_t hProcess, void *lpBaseAddress,
                               uintptr_t dwSize);
uintptr_t GetCurrentProcess(void);
#endif

#if defined(__FreeBSD__) && defined(__arm__)
// clang-format off
#include <sys/types.h>
#include <machine/sysarch.h>
// clang-format on
#endif

#if defined(__NetBSD__) && defined(__arm__)
#include <machine/sysarch.h>
#endif

#if defined(__OpenBSD__) && (defined(__arm__) || defined(__mips__) || defined(__riscv))
// clang-format off
#include <sys/types.h>
#include <machine/sysarch.h>
// clang-format on
#endif

#if defined(__linux__) && defined(__mips__)
#include <sys/cachectl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if defined(__linux__) && defined(__riscv)
// to get platform-specific syscall definitions
#include <linux/unistd.h>
#endif

// The compiler generates calls to __clear_cache() when creating
// trampoline functions on the stack for use with nested functions.
// It is expected to invalidate the instruction cache for the
// specified range.

void __clear_cache(void *start, void *end) {
#if __i386__ || __x86_64__ || defined(_M_IX86) || defined(_M_X64)
// Intel processors have a unified instruction and data cache
// so there is nothing to do
#elif defined(_WIN32) && (defined(__arm__) || defined(__aarch64__))
  FlushInstructionCache(GetCurrentProcess(), start, end - start);
#elif defined(__arm__) && !defined(__APPLE__)
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
  struct arm_sync_icache_args arg;

  arg.addr = (uintptr_t)start;
  arg.len = (uintptr_t)end - (uintptr_t)start;

  sysarch(ARM_SYNC_ICACHE, &arg);
#elif defined(__linux__)
// We used to include asm/unistd.h for the __ARM_NR_cacheflush define, but
// it also brought many other unused defines, as well as a dependency on
// kernel headers to be installed.
//
// This value is stable at least since Linux 3.13 and should remain so for
// compatibility reasons, warranting it's re-definition here.
#define __ARM_NR_cacheflush 0x0f0002
  register int start_reg __asm("r0") = (int)(intptr_t)start;
  const register int end_reg __asm("r1") = (int)(intptr_t)end;
  const register int flags __asm("r2") = 0;
  const register int syscall_nr __asm("r7") = __ARM_NR_cacheflush;
  __asm __volatile("svc 0x0"
                   : "=r"(start_reg)
                   : "r"(syscall_nr), "r"(start_reg), "r"(end_reg), "r"(flags));
  assert(start_reg == 0 && "Cache flush syscall failed.");
#else
  compilerrt_abort();
#endif
#elif defined(__linux__) && defined(__loongarch__)
  __asm__ volatile("ibar 0");
#elif defined(__mips__)
  const uintptr_t start_int = (uintptr_t)start;
  const uintptr_t end_int = (uintptr_t)end;
  uintptr_t synci_step;
  __asm__ volatile("rdhwr %0, $1" : "=r"(synci_step));
  if (synci_step != 0) {
#if __mips_isa_rev >= 6
    for (uintptr_t p = start_int; p < end_int; p += synci_step)
      __asm__ volatile("synci 0(%0)" : : "r"(p));

    // The last "move $at, $0" is the target of jr.hb instead of delay slot.
    __asm__ volatile(".set noat\n"
                     "sync\n"
                     "addiupc $at, 12\n"
                     "jr.hb $at\n"
                     "move $at, $0\n"
                     ".set at");
#elif defined(__linux__) || defined(__OpenBSD__)
    // Pre-R6 may not be globalized. And some implementations may give strange
    // synci_step. So, let's use libc call for it.
    _flush_cache(start, end_int - start_int, BCACHE);
#else
    (void)start_int;
    (void)end_int;
    compilerrt_abort();
#endif
  }
#elif defined(__aarch64__) && !defined(__APPLE__)
  uint64_t xstart = (uint64_t)(uintptr_t)start;
  uint64_t xend = (uint64_t)(uintptr_t)end;

  // Get Cache Type Info.
  static uint64_t ctr_el0 = 0;
  if (ctr_el0 == 0)
    __asm __volatile("mrs %0, ctr_el0" : "=r"(ctr_el0));

  // The DC and IC instructions must use 64-bit registers so we don't use
  // uintptr_t in case this runs in an IPL32 environment.
  uint64_t addr;

  // If CTR_EL0.IDC is set, data cache cleaning to the point of unification
  // is not required for instruction to data coherence.
  if (((ctr_el0 >> 28) & 0x1) == 0x0) {
    const size_t dcache_line_size = 4 << ((ctr_el0 >> 16) & 15);
    for (addr = xstart & ~(dcache_line_size - 1); addr < xend;
         addr += dcache_line_size)
      __asm __volatile("dc cvau, %0" ::"r"(addr));
  }
  __asm __volatile("dsb ish");

  // If CTR_EL0.DIC is set, instruction cache invalidation to the point of
  // unification is not required for instruction to data coherence.
  if (((ctr_el0 >> 29) & 0x1) == 0x0) {
    const size_t icache_line_size = 4 << ((ctr_el0 >> 0) & 15);
    for (addr = xstart & ~(icache_line_size - 1); addr < xend;
         addr += icache_line_size)
      __asm __volatile("ic ivau, %0" ::"r"(addr));
    __asm __volatile("dsb ish");
  }
  __asm __volatile("isb sy");
#elif defined(__powerpc__)
  // Newer CPUs have a bigger line size made of multiple blocks, so the
  // following value is a minimal common denominator for what used to be
  // a single block cache line and is therefore inneficient.
  const size_t line_size = 32;
  const size_t len = (uintptr_t)end - (uintptr_t)start;

  const uintptr_t mask = ~(line_size - 1);
  const uintptr_t start_line = ((uintptr_t)start) & mask;
  const uintptr_t end_line = ((uintptr_t)start + len + line_size - 1) & mask;

  for (uintptr_t line = start_line; line < end_line; line += line_size)
    __asm__ volatile("dcbf 0, %0" : : "r"(line));
  __asm__ volatile("sync");

  for (uintptr_t line = start_line; line < end_line; line += line_size)
    __asm__ volatile("icbi 0, %0" : : "r"(line));
  __asm__ volatile("isync");
#elif defined(__sparc__)
  const size_t dword_size = 8;
  const size_t len = (uintptr_t)end - (uintptr_t)start;

  const uintptr_t mask = ~(dword_size - 1);
  const uintptr_t start_dword = ((uintptr_t)start) & mask;
  const uintptr_t end_dword = ((uintptr_t)start + len + dword_size - 1) & mask;

  for (uintptr_t dword = start_dword; dword < end_dword; dword += dword_size)
    __asm__ volatile("flush %0" : : "r"(dword));
#elif defined(__riscv) && defined(__linux__)
  // See: arch/riscv/include/asm/cacheflush.h, arch/riscv/kernel/sys_riscv.c
  register void *start_reg __asm("a0") = start;
  const register void *end_reg __asm("a1") = end;
  // "0" means that we clear cache for all threads (SYS_RISCV_FLUSH_ICACHE_ALL)
  const register long flags __asm("a2") = 0;
  const register long syscall_nr __asm("a7") = __NR_riscv_flush_icache;
  __asm __volatile("ecall"
                   : "=r"(start_reg)
                   : "r"(start_reg), "r"(end_reg), "r"(flags), "r"(syscall_nr));
  assert(start_reg == 0 && "Cache flush syscall failed.");
#elif defined(__riscv) && defined(__OpenBSD__)
  struct riscv_sync_icache_args arg;

  arg.addr = (uintptr_t)start;
  arg.len = (uintptr_t)end - (uintptr_t)start;

  sysarch(RISCV_SYNC_ICACHE, &arg);
#elif defined(__ve__)
  __asm__ volatile("fencec 2");
#else
#if __APPLE__
  // On Darwin, sys_icache_invalidate() provides this functionality
  sys_icache_invalidate(start, end - start);
#else
  compilerrt_abort();
#endif
#endif
}
