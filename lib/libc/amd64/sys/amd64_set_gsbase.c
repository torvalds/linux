/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Peter Wemm
 * Copyright (c) 2017, 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
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

#define	IN_RTLD	1
#include <sys/param.h>
#undef IN_RTLD
#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#include <machine/sysarch.h>
#include <x86/ifunc.h>
#include "libc_private.h"

static int
amd64_set_gsbase_cpu(void *addr)
{

	wrgsbase((uintptr_t)addr);
	return (0);
}

static int
amd64_set_gsbase_syscall(void *addr)
{

	return (sysarch(AMD64_SET_GSBASE, &addr));
}

DEFINE_UIFUNC(, int, amd64_set_gsbase, (void *), static)
{

	if (__getosreldate() >= P_OSREL_WRFSBASE &&
	    (cpu_stdext_feature & CPUID_STDEXT_FSGSBASE) != 0)
		return (amd64_set_gsbase_cpu);
	return (amd64_set_gsbase_syscall);
}
