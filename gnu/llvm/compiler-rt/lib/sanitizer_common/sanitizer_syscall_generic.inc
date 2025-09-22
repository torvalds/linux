//===-- sanitizer_syscall_generic.inc ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic implementations of internal_syscall* and internal_iserror.
//
//===----------------------------------------------------------------------===//

// NetBSD uses libc calls directly
#if !SANITIZER_NETBSD

#if SANITIZER_FREEBSD || SANITIZER_APPLE || SANITIZER_SOLARIS
# define SYSCALL(name) SYS_ ## name
#else
# define SYSCALL(name) __NR_ ## name
#endif

#if (defined(__x86_64__) && (SANITIZER_FREEBSD || SANITIZER_APPLE)) || \
    (defined(__aarch64__) && SANITIZER_FREEBSD)
# define internal_syscall __syscall
# else
# define internal_syscall syscall
#endif

#endif

bool internal_iserror(uptr retval, int *rverrno) {
  if (retval == (uptr)-1) {
    if (rverrno)
      *rverrno = errno;
    return true;
  } else {
    return false;
  }
}
