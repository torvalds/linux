//===-- sanitizer_syscall_linux_hexagon.inc ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementations of internal_syscall and internal_iserror for Linux/hexagon.
//
//===----------------------------------------------------------------------===//

#define SYSCALL(name) __NR_##name

#define __internal_syscall_LL_E(x) \
  ((union {                        \
    long long ll;                  \
    long l[2];                     \
  }){.ll = x})                     \
      .l[0],                       \
      ((union {                    \
        long long ll;              \
        long l[2];                 \
      }){.ll = x})                 \
          .l[1]
#define __internal_syscall_LL_O(x) 0, __SYSCALL_LL_E((x))

#define __asm_syscall(...)                                                 \
  do {                                                                     \
    __asm__ __volatile__("trap0(#1)" : "=r"(r0) : __VA_ARGS__ : "memory"); \
    return r0;                                                             \
  } while (0)

#define __internal_syscall0(n) (__internal_syscall)(n)

static uptr __internal_syscall(long n) {
  register u32 r6 __asm__("r6") = n;
  register u32 r0 __asm__("r0");
  __asm_syscall("r"(r6));
}

#define __internal_syscall1(n, a1) (__internal_syscall)(n, (long)(a1))

static uptr __internal_syscall(long n, long a) {
  register u32 r6 __asm__("r6") = n;
  register u32 r0 __asm__("r0") = a;
  __asm_syscall("r"(r6), "0"(r0));
}

#define __internal_syscall2(n, a1, a2) \
  (__internal_syscall)(n, (long)(a1), (long)(a2))

static uptr __internal_syscall(long n, long a, long b) {
  register u32 r6 __asm__("r6") = n;
  register u32 r0 __asm__("r0") = a;
  register u32 r1 __asm__("r1") = b;
  __asm_syscall("r"(r6), "0"(r0), "r"(r1));
}

#define __internal_syscall3(n, a1, a2, a3) \
  (__internal_syscall)(n, (long)(a1), (long)(a2), (long)(a3))

static uptr __internal_syscall(long n, long a, long b, long c) {
  register u32 r6 __asm__("r6") = n;
  register u32 r0 __asm__("r0") = a;
  register u32 r1 __asm__("r1") = b;
  register u32 r2 __asm__("r2") = c;
  __asm_syscall("r"(r6), "0"(r0), "r"(r1), "r"(r2));
}

#define __internal_syscall4(n, a1, a2, a3, a4) \
  (__internal_syscall)(n, (long)(a1), (long)(a2), (long)(a3), (long)(a4))

static uptr __internal_syscall(long n, long a, long b, long c, long d) {
  register u32 r6 __asm__("r6") = n;
  register u32 r0 __asm__("r0") = a;
  register u32 r1 __asm__("r1") = b;
  register u32 r2 __asm__("r2") = c;
  register u32 r3 __asm__("r3") = d;
  __asm_syscall("r"(r6), "0"(r0), "r"(r1), "r"(r2), "r"(r3));
}

#define __internal_syscall5(n, a1, a2, a3, a4, a5)                        \
  (__internal_syscall)(n, (long)(a1), (long)(a2), (long)(a3), (long)(a4), \
                       (long)(a5))

static uptr __internal_syscall(long n, long a, long b, long c, long d, long e) {
  register u32 r6 __asm__("r6") = n;
  register u32 r0 __asm__("r0") = a;
  register u32 r1 __asm__("r1") = b;
  register u32 r2 __asm__("r2") = c;
  register u32 r3 __asm__("r3") = d;
  register u32 r4 __asm__("r4") = e;
  __asm_syscall("r"(r6), "0"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4));
}

#define __internal_syscall6(n, a1, a2, a3, a4, a5, a6)                    \
  (__internal_syscall)(n, (long)(a1), (long)(a2), (long)(a3), (long)(a4), \
                       (long)(a5), (long)(a6))

static uptr __internal_syscall(long n, long a, long b, long c, long d, long e,
                               long f) {
  register u32 r6 __asm__("r6") = n;
  register u32 r0 __asm__("r0") = a;
  register u32 r1 __asm__("r1") = b;
  register u32 r2 __asm__("r2") = c;
  register u32 r3 __asm__("r3") = d;
  register u32 r4 __asm__("r4") = e;
  register u32 r5 __asm__("r5") = f;
  __asm_syscall("r"(r6), "0"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5));
}

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
