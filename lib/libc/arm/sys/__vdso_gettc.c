/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 The FreeBSD Foundation
 *
 * This software was developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/elf.h>
#include <sys/time.h>
#include <sys/vdso.h>
#include <machine/cpufunc.h>
#include <machine/acle-compat.h>
#include <errno.h>
#include "libc_private.h"

#if __ARM_ARCH >= 6
static inline uint64_t
cp15_cntvct_get(void)
{
	uint64_t reg;

	__asm __volatile("mrrc\tp15, 1, %Q0, %R0, c14" : "=r" (reg));
	return (reg);
}

static inline uint64_t
cp15_cntpct_get(void)
{
	uint64_t reg;

	__asm __volatile("mrrc\tp15, 0, %Q0, %R0, c14" : "=r" (reg));
	return (reg);
}
#endif

#pragma weak __vdso_gettc
int
__vdso_gettc(const struct vdso_timehands *th, u_int *tc)
{

	if (th->th_algo != VDSO_TH_ALGO_ARM_GENTIM)
		return (ENOSYS);
#if __ARM_ARCH >= 6
	/*
	 * Userspace gettimeofday() is only enabled on ARMv7 CPUs, but
	 * libc is compiled for ARMv6.  Due to clang issues, .arch
	 * armv7-a directive does not work.
	 */
	__asm __volatile(".word\t0xf57ff06f" : : : "memory"); /* isb */
	*tc = th->th_physical == 0 ? cp15_cntvct_get() : cp15_cntpct_get();
	return (0);
#else
	*tc = 0;
	return (ENOSYS);
#endif
}

#pragma weak __vdso_gettimekeep
int
__vdso_gettimekeep(struct vdso_timekeep **tk)
{

	return (_elf_aux_info(AT_TIMEKEEP, tk, sizeof(*tk)));
}
