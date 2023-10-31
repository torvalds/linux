/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023 WANG Xuerui <git@xen0n.name>
 *
 * raid6/loongarch.h
 *
 * Definitions common to LoongArch RAID-6 code only
 */

#ifndef _LIB_RAID6_LOONGARCH_H
#define _LIB_RAID6_LOONGARCH_H

#ifdef __KERNEL__

#include <asm/cpu-features.h>
#include <asm/fpu.h>

#else /* for user-space testing */

#include <sys/auxv.h>

/* have to supply these defines for glibc 2.37- and musl */
#ifndef HWCAP_LOONGARCH_LSX
#define HWCAP_LOONGARCH_LSX	(1 << 4)
#endif
#ifndef HWCAP_LOONGARCH_LASX
#define HWCAP_LOONGARCH_LASX	(1 << 5)
#endif

#define kernel_fpu_begin()
#define kernel_fpu_end()

#define cpu_has_lsx	(getauxval(AT_HWCAP) & HWCAP_LOONGARCH_LSX)
#define cpu_has_lasx	(getauxval(AT_HWCAP) & HWCAP_LOONGARCH_LASX)

#endif /* __KERNEL__ */

#endif /* _LIB_RAID6_LOONGARCH_H */
