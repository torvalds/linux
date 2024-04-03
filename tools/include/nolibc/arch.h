/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

/* Below comes the architecture-specific code. For each architecture, we have
 * the syscall declarations and the _start code definition. This is the only
 * global part. On all architectures the kernel puts everything in the stack
 * before jumping to _start just above us, without any return address (_start
 * is not a function but an entry point). So at the stack pointer we find argc.
 * Then argv[] begins, and ends at the first NULL. Then we have envp which
 * starts and ends with a NULL as well. So envp=argv+argc+1.
 */

#ifndef _NOLIBC_ARCH_H
#define _NOLIBC_ARCH_H

#if defined(__x86_64__)
#include "arch-x86_64.h"
#elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
#include "arch-i386.h"
#elif defined(__ARM_EABI__)
#include "arch-arm.h"
#elif defined(__aarch64__)
#include "arch-aarch64.h"
#elif defined(__mips__)
#include "arch-mips.h"
#elif defined(__powerpc__)
#include "arch-powerpc.h"
#elif defined(__riscv)
#include "arch-riscv.h"
#elif defined(__s390x__)
#include "arch-s390.h"
#elif defined(__loongarch__)
#include "arch-loongarch.h"
#else
#error Unsupported Architecture
#endif

#endif /* _NOLIBC_ARCH_H */
