/*
 * include/asm-generic/seccomp.h
 *
 * Copyright (C) 2014 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi <at> linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASM_GENERIC_SECCOMP_H
#define _ASM_GENERIC_SECCOMP_H

#include <asm-generic/unistd.h>

#if defined(CONFIG_COMPAT) && !defined(__NR_seccomp_read_32)
#define __NR_seccomp_read_32		__NR_read
#define __NR_seccomp_write_32		__NR_write
#define __NR_seccomp_exit_32		__NR_exit
#define __NR_seccomp_sigreturn_32	__NR_rt_sigreturn
#endif /* CONFIG_COMPAT && ! already defined */

#define __NR_seccomp_read		__NR_read
#define __NR_seccomp_write		__NR_write
#define __NR_seccomp_exit		__NR_exit
#define __NR_seccomp_sigreturn		__NR_rt_sigreturn

#endif /* _ASM_GENERIC_SECCOMP_H */

