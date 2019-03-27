/*-
 * Copyright (c) 2019 The FreeBSD Foundation
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

#include <sys/param.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#include <machine/sysarch.h>
#include <x86/ifunc.h>
#include <errno.h>
#include <string.h>

#define	MAX_PKRU_IDX	0xf
#ifdef __i386__
#define	X86_SET_PKRU	I386_SET_PKRU
#define	X86_CLEAR_PKRU	I386_CLEAR_PKRU
#else
#define	X86_SET_PKRU	AMD64_SET_PKRU
#define	X86_CLEAR_PKRU	AMD64_CLEAR_PKRU
#endif

static int
x86_pkru_get_perm_unsup(u_int keyidx, int *access, int *modify)
{

	errno = EOPNOTSUPP;
	return (-1);
}

static int
x86_pkru_get_perm_hw(u_int keyidx, int *access, int *modify)
{
	uint32_t pkru;

	if (keyidx > MAX_PKRU_IDX) {
		errno = EINVAL;
		return (-1);
	}
	keyidx *= 2;
	pkru = rdpkru();
	*access = (pkru & (1 << keyidx)) == 0;
	*modify = (pkru & (2 << keyidx)) == 0;
	return (0);
}

DEFINE_UIFUNC(, int, x86_pkru_get_perm, (u_int, int *, int *), static)
{

	return ((cpu_stdext_feature2 & CPUID_STDEXT2_OSPKE) == 0 ?
	    x86_pkru_get_perm_unsup : x86_pkru_get_perm_hw);
}

static int
x86_pkru_set_perm_unsup(u_int keyidx, int access, int modify)
{

	errno = EOPNOTSUPP;
	return (-1);
}

static int
x86_pkru_set_perm_hw(u_int keyidx, int access, int modify)
{
	uint32_t pkru;

	if (keyidx > MAX_PKRU_IDX) {
		errno = EINVAL;
		return (-1);
	}
	keyidx *= 2;
	pkru = rdpkru();
	pkru &= ~(3 << keyidx);
	if (!access)
		pkru |= 1 << keyidx;
	if (!modify)
		pkru |= 2 << keyidx;
	wrpkru(pkru);
	return (0);
}

DEFINE_UIFUNC(, int, x86_pkru_set_perm, (u_int, int, int), static)
{

	return ((cpu_stdext_feature2 & CPUID_STDEXT2_OSPKE) == 0 ?
	    x86_pkru_set_perm_unsup : x86_pkru_set_perm_hw);
}

int
x86_pkru_protect_range(void *addr, unsigned long len, u_int keyidx, int flags)
{
	struct amd64_set_pkru a64pkru;

	memset(&a64pkru, 0, sizeof(a64pkru));
	a64pkru.addr = addr;
	a64pkru.len = len;
	a64pkru.keyidx = keyidx;
	a64pkru.flags = flags;
	return (sysarch(X86_SET_PKRU, &a64pkru));
}

int
x86_pkru_unprotect_range(void *addr, unsigned long len)
{
	struct amd64_set_pkru a64pkru;

	memset(&a64pkru, 0, sizeof(a64pkru));
	a64pkru.addr = addr;
	a64pkru.len = len;
	return (sysarch(X86_CLEAR_PKRU, &a64pkru));
}
