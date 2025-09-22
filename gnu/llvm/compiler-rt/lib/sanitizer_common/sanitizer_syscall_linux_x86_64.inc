//===-- sanitizer_syscall_linux_x86_64.inc ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementations of internal_syscall and internal_iserror for Linux/x86_64.
//
//===----------------------------------------------------------------------===//

#define SYSCALL(name) __NR_ ## name

static uptr internal_syscall(u64 nr) {
  u64 retval;
  asm volatile("syscall" : "=a"(retval) : "a"(nr) : "rcx", "r11",
               "memory", "cc");
  return retval;
}

template <typename T1>
static uptr internal_syscall(u64 nr, T1 arg1) {
  u64 retval;
  asm volatile("syscall" : "=a"(retval) : "a"(nr), "D"((u64)arg1) :
               "rcx", "r11", "memory", "cc");
  return retval;
}

template <typename T1, typename T2>
static uptr internal_syscall(u64 nr, T1 arg1, T2 arg2) {
  u64 retval;
  asm volatile("syscall" : "=a"(retval) : "a"(nr), "D"((u64)arg1),
               "S"((u64)arg2) : "rcx", "r11", "memory", "cc");
  return retval;
}

template <typename T1, typename T2, typename T3>
static uptr internal_syscall(u64 nr, T1 arg1, T2 arg2, T3 arg3) {
  u64 retval;
  asm volatile("syscall" : "=a"(retval) : "a"(nr), "D"((u64)arg1),
               "S"((u64)arg2), "d"((u64)arg3) : "rcx", "r11", "memory", "cc");
  return retval;
}

template <typename T1, typename T2, typename T3, typename T4>
static uptr internal_syscall(u64 nr, T1 arg1, T2 arg2, T3 arg3, T4 arg4) {
  u64 retval;
  asm volatile("mov %5, %%r10;"
               "syscall" : "=a"(retval) : "a"(nr), "D"((u64)arg1),
               "S"((u64)arg2), "d"((u64)arg3), "r"((u64)arg4) :
               "rcx", "r11", "r10", "memory", "cc");
  return retval;
}

template <typename T1, typename T2, typename T3, typename T4, typename T5>
static uptr internal_syscall(u64 nr, T1 arg1, T2 arg2, T3 arg3, T4 arg4,
                             T5 arg5) {
  u64 retval;
  asm volatile("mov %5, %%r10;"
               "mov %6, %%r8;"
               "syscall" : "=a"(retval) : "a"(nr), "D"((u64)arg1),
               "S"((u64)arg2), "d"((u64)arg3), "r"((u64)arg4), "r"((u64)arg5) :
               "rcx", "r11", "r10", "r8", "memory", "cc");
  return retval;
}

template <typename T1, typename T2, typename T3, typename T4, typename T5,
          typename T6>
static uptr internal_syscall(u64 nr, T1 arg1, T2 arg2, T3 arg3, T4 arg4,
                             T5 arg5, T6 arg6) {
  u64 retval;
  asm volatile("mov %5, %%r10;"
               "mov %6, %%r8;"
               "mov %7, %%r9;"
               "syscall" : "=a"(retval) : "a"(nr), "D"((u64)arg1),
               "S"((u64)arg2), "d"((u64)arg3), "r"((u64)arg4), "r"((u64)arg5),
               "r"((u64)arg6) : "rcx", "r11", "r10", "r8", "r9",
               "memory", "cc");
  return retval;
}

bool internal_iserror(uptr retval, int *rverrno) {
  if (retval >= (uptr)-4095) {
    if (rverrno)
      *rverrno = -retval;
    return true;
  }
  return false;
}
