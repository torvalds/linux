/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#define __ASSEMBLY__

#if defined(__aarch64__)
#include "../../../../arch/arm64/kernel/vdso/vgetrandom-chacha.S"
#elif defined(__loongarch__)
#include "../../../../arch/loongarch/vdso/vgetrandom-chacha.S"
#elif defined(__powerpc__) || defined(__powerpc64__)
#include "../../../../arch/powerpc/kernel/vdso/vgetrandom-chacha.S"
#elif defined(__s390x__)
#include "../../../../arch/s390/kernel/vdso64/vgetrandom-chacha.S"
#elif defined(__x86_64__)
#include "../../../../arch/x86/entry/vdso/vgetrandom-chacha.S"
#endif
