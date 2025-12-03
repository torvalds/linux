/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_ARCH_H
#define _NOLIBC_ARCH_H

#if defined(__x86_64__) || defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
#include "arch-x86.h"
#elif defined(__ARM_EABI__)
#include "arch-arm.h"
#elif defined(__aarch64__)
#include "arch-arm64.h"
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
#elif defined(__sparc__)
#include "arch-sparc.h"
#elif defined(__m68k__)
#include "arch-m68k.h"
#elif defined(__sh__)
#include "arch-sh.h"
#else
#error Unsupported Architecture
#endif

#endif /* _NOLIBC_ARCH_H */
