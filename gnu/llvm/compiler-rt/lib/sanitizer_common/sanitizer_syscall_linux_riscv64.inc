//===-- sanitizer_syscall_linux_riscv64.inc ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementations of internal_syscall and internal_iserror for Linux/riscv64.
//
//===----------------------------------------------------------------------===//

// About local register variables:
// https://gcc.gnu.org/onlinedocs/gcc/Local-Register-Variables.html#Local-Register-Variables
//
// Kernel ABI...
// To my surprise I haven't found much information regarding it.
// Kernel source and internet browsing shows that:
//  syscall number is passed in a7
//  (http://man7.org/linux/man-pages/man2/syscall.2.html) results are return in
//  a0 and a1 (http://man7.org/linux/man-pages/man2/syscall.2.html) arguments
//  are passed in: a0-a7 (see below)
//
//  Regarding the arguments. The only "documentation" I could find is
//  this comment (!!!) by Bruce Hold on google forums (!!!):
//    https://groups.google.com/a/groups.riscv.org/forum/#!topic/sw-dev/exbrzM3GZDQ
//    Confirmed by inspecting glibc sources.
//  Great way to document things.
#define SYSCALL(name) __NR_##name

#define INTERNAL_SYSCALL_CLOBBERS "memory"

static uptr __internal_syscall(u64 nr) {
  register u64 a7 asm("a7") = nr;
  register u64 a0 asm("a0");
  __asm__ volatile("ecall\n\t"
                   : "=r"(a0)
                   : "r"(a7)
                   : INTERNAL_SYSCALL_CLOBBERS);
  return a0;
}
#define __internal_syscall0(n) (__internal_syscall)(n)

static uptr __internal_syscall(u64 nr, u64 arg1) {
  register u64 a7 asm("a7") = nr;
  register u64 a0 asm("a0") = arg1;
  __asm__ volatile("ecall\n\t"
                   : "+r"(a0)
                   : "r"(a7)
                   : INTERNAL_SYSCALL_CLOBBERS);
  return a0;
}
#define __internal_syscall1(n, a1) (__internal_syscall)(n, (u64)(a1))

static uptr __internal_syscall(u64 nr, u64 arg1, long arg2) {
  register u64 a7 asm("a7") = nr;
  register u64 a0 asm("a0") = arg1;
  register u64 a1 asm("a1") = arg2;
  __asm__ volatile("ecall\n\t"
                   : "+r"(a0)
                   : "r"(a7), "r"(a1)
                   : INTERNAL_SYSCALL_CLOBBERS);
  return a0;
}
#define __internal_syscall2(n, a1, a2) \
  (__internal_syscall)(n, (u64)(a1), (long)(a2))

static uptr __internal_syscall(u64 nr, u64 arg1, long arg2, long arg3) {
  register u64 a7 asm("a7") = nr;
  register u64 a0 asm("a0") = arg1;
  register u64 a1 asm("a1") = arg2;
  register u64 a2 asm("a2") = arg3;
  __asm__ volatile("ecall\n\t"
                   : "+r"(a0)
                   : "r"(a7), "r"(a1), "r"(a2)
                   : INTERNAL_SYSCALL_CLOBBERS);
  return a0;
}
#define __internal_syscall3(n, a1, a2, a3) \
  (__internal_syscall)(n, (u64)(a1), (long)(a2), (long)(a3))

static uptr __internal_syscall(u64 nr, u64 arg1, long arg2, long arg3,
                               u64 arg4) {
  register u64 a7 asm("a7") = nr;
  register u64 a0 asm("a0") = arg1;
  register u64 a1 asm("a1") = arg2;
  register u64 a2 asm("a2") = arg3;
  register u64 a3 asm("a3") = arg4;
  __asm__ volatile("ecall\n\t"
                   : "+r"(a0)
                   : "r"(a7), "r"(a1), "r"(a2), "r"(a3)
                   : INTERNAL_SYSCALL_CLOBBERS);
  return a0;
}
#define __internal_syscall4(n, a1, a2, a3, a4) \
  (__internal_syscall)(n, (u64)(a1), (long)(a2), (long)(a3), (long)(a4))

static uptr __internal_syscall(u64 nr, u64 arg1, long arg2, long arg3, u64 arg4,
                               long arg5) {
  register u64 a7 asm("a7") = nr;
  register u64 a0 asm("a0") = arg1;
  register u64 a1 asm("a1") = arg2;
  register u64 a2 asm("a2") = arg3;
  register u64 a3 asm("a3") = arg4;
  register u64 a4 asm("a4") = arg5;
  __asm__ volatile("ecall\n\t"
                   : "+r"(a0)
                   : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4)
                   : INTERNAL_SYSCALL_CLOBBERS);
  return a0;
}
#define __internal_syscall5(n, a1, a2, a3, a4, a5)                       \
  (__internal_syscall)(n, (u64)(a1), (long)(a2), (long)(a3), (long)(a4), \
                       (u64)(a5))

static uptr __internal_syscall(u64 nr, u64 arg1, long arg2, long arg3, u64 arg4,
                               long arg5, long arg6) {
  register u64 a7 asm("a7") = nr;
  register u64 a0 asm("a0") = arg1;
  register u64 a1 asm("a1") = arg2;
  register u64 a2 asm("a2") = arg3;
  register u64 a3 asm("a3") = arg4;
  register u64 a4 asm("a4") = arg5;
  register u64 a5 asm("a5") = arg6;
  __asm__ volatile("ecall\n\t"
                   : "+r"(a0)
                   : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5)
                   : INTERNAL_SYSCALL_CLOBBERS);
  return a0;
}
#define __internal_syscall6(n, a1, a2, a3, a4, a5, a6)                   \
  (__internal_syscall)(n, (u64)(a1), (long)(a2), (long)(a3), (long)(a4), \
                       (u64)(a5), (long)(a6))

static uptr __internal_syscall(u64 nr, u64 arg1, long arg2, long arg3, u64 arg4,
                               long arg5, long arg6, long arg7) {
  register u64 a7 asm("a7") = nr;
  register u64 a0 asm("a0") = arg1;
  register u64 a1 asm("a1") = arg2;
  register u64 a2 asm("a2") = arg3;
  register u64 a3 asm("a3") = arg4;
  register u64 a4 asm("a4") = arg5;
  register u64 a5 asm("a5") = arg6;
  register u64 a6 asm("a6") = arg7;
  __asm__ volatile("ecall\n\t"
                   : "+r"(a0)
                   : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                     "r"(a6)
                   : INTERNAL_SYSCALL_CLOBBERS);
  return a0;
}
#define __internal_syscall7(n, a1, a2, a3, a4, a5, a6, a7)               \
  (__internal_syscall)(n, (u64)(a1), (long)(a2), (long)(a3), (long)(a4), \
                       (u64)(a5), (long)(a6), (long)(a7))

#define __SYSCALL_NARGS_X(a1, a2, a3, a4, a5, a6, a7, a8, n, ...) n
#define __SYSCALL_NARGS(...) \
  __SYSCALL_NARGS_X(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0, )
#define __SYSCALL_CONCAT_X(a, b) a##b
#define __SYSCALL_CONCAT(a, b) __SYSCALL_CONCAT_X(a, b)
#define __SYSCALL_DISP(b, ...) \
  __SYSCALL_CONCAT(b, __SYSCALL_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define internal_syscall(...) __SYSCALL_DISP(__internal_syscall, __VA_ARGS__)

// Helper function used to avoid clobbering of errno.
bool internal_iserror(uptr retval, int *rverrno) {
  if (retval >= (uptr)-4095) {
    if (rverrno)
      *rverrno = -retval;
    return true;
  }
  return false;
}
