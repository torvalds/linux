/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * Copyright (c) 2013 Andrew Turner
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * Bases on NetBSD lib/libc/arch/arm/misc/arm_initfini.c
 * $NetBSD: arm_initfini.c,v 1.2 2013/01/31 06:47:55 matt Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * To properly implement setjmp/longjmp for the ARM AAPCS ABI, it has to be
 * aware of whether there is a FPU is present or not.  Regardless of whether
 * the hard-float ABI is being used, setjmp needs to save D8-D15.  But it can
 * only do this if those instructions won't cause an exception.
 */

#include <sys/param.h>
#include <sys/sysctl.h>

#include <stdbool.h>
#include <stddef.h>

extern int __sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen);

int _libc_arm_fpu_present;
static bool _libc_aapcs_initialized;

void	_libc_aapcs_init(void) __attribute__((__constructor__, __used__));

void
_libc_aapcs_init(void)
{
	int mib[2];
	size_t len;

	if (_libc_aapcs_initialized)
		return;

	mib[0] = CTL_HW;
	mib[1] = HW_FLOATINGPT;

	len = sizeof(_libc_arm_fpu_present);
	if (__sysctl(mib, 2, &_libc_arm_fpu_present, &len, NULL, 0) == -1 ||
	    len != sizeof(_libc_arm_fpu_present)) {
		/* sysctl failed, assume no vfp */
		_libc_arm_fpu_present = 0;
	}

	_libc_aapcs_initialized = true;
}
