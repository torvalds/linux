/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/systm.h>

#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <x86/ifunc.h>

int fubyte_nosmap(volatile const void *base);
int fubyte_smap(volatile const void *base);
DEFINE_IFUNC(, int, fubyte, (volatile const void *), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    fubyte_smap : fubyte_nosmap);
}

int fuword16_nosmap(volatile const void *base);
int fuword16_smap(volatile const void *base);
DEFINE_IFUNC(, int, fuword16, (volatile const void *), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    fuword16_smap : fuword16_nosmap);
}

int fueword_nosmap(volatile const void *base, long *val);
int fueword_smap(volatile const void *base, long *val);
DEFINE_IFUNC(, int, fueword, (volatile const void *, long *), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    fueword_smap : fueword_nosmap);
}
DEFINE_IFUNC(, int, fueword64, (volatile const void *, int64_t *), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    fueword_smap : fueword_nosmap);
}

int	fueword32_nosmap(volatile const void *base, int32_t *val);
int	fueword32_smap(volatile const void *base, int32_t *val);
DEFINE_IFUNC(, int, fueword32, (volatile const void *, int32_t *), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    fueword32_smap : fueword32_nosmap);
}

int	subyte_nosmap(volatile void *base, int byte);
int	subyte_smap(volatile void *base, int byte);
DEFINE_IFUNC(, int, subyte, (volatile void *, int), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    subyte_smap : subyte_nosmap);
}

int	suword16_nosmap(volatile void *base, int word);
int	suword16_smap(volatile void *base, int word);
DEFINE_IFUNC(, int, suword16, (volatile void *, int), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    suword16_smap : suword16_nosmap);
}

int	suword32_nosmap(volatile void *base, int32_t word);
int	suword32_smap(volatile void *base, int32_t word);
DEFINE_IFUNC(, int, suword32, (volatile void *, int32_t), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    suword32_smap : suword32_nosmap);
}

int	suword_nosmap(volatile void *base, long word);
int	suword_smap(volatile void *base, long word);
DEFINE_IFUNC(, int, suword, (volatile void *, long), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    suword_smap : suword_nosmap);
}
DEFINE_IFUNC(, int, suword64, (volatile void *, int64_t), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    suword_smap : suword_nosmap);
}

int	casueword32_nosmap(volatile uint32_t *base, uint32_t oldval,
	    uint32_t *oldvalp, uint32_t newval);
int	casueword32_smap(volatile uint32_t *base, uint32_t oldval,
	    uint32_t *oldvalp, uint32_t newval);
DEFINE_IFUNC(, int, casueword32, (volatile uint32_t *, uint32_t, uint32_t *,
    uint32_t), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    casueword32_smap : casueword32_nosmap);
}

int	casueword_nosmap(volatile u_long *p, u_long oldval, u_long *oldvalp,
	    u_long newval);
int	casueword_smap(volatile u_long *p, u_long oldval, u_long *oldvalp,
	    u_long newval);
DEFINE_IFUNC(, int, casueword, (volatile u_long *, u_long, u_long *, u_long),
    static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    casueword_smap : casueword_nosmap);
}

int	copyinstr_nosmap(const void *udaddr, void *kaddr, size_t len,
	    size_t *lencopied);
int	copyinstr_smap(const void *udaddr, void *kaddr, size_t len,
	    size_t *lencopied);
DEFINE_IFUNC(, int, copyinstr, (const void *, void *, size_t, size_t *),
    static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    copyinstr_smap : copyinstr_nosmap);
}

int	copyin_nosmap_std(const void *udaddr, void *kaddr, size_t len);
int	copyin_smap_std(const void *udaddr, void *kaddr, size_t len);
int	copyin_nosmap_erms(const void *udaddr, void *kaddr, size_t len);
int	copyin_smap_erms(const void *udaddr, void *kaddr, size_t len);
DEFINE_IFUNC(, int, copyin, (const void *, void *, size_t), static)
{

	switch (cpu_stdext_feature & (CPUID_STDEXT_SMAP | CPUID_STDEXT_ERMS)) {
	case CPUID_STDEXT_SMAP:
		return (copyin_smap_std);
	case CPUID_STDEXT_ERMS:
		return (copyin_nosmap_erms);
	case CPUID_STDEXT_SMAP | CPUID_STDEXT_ERMS:
		return (copyin_smap_erms);
	default:
		return (copyin_nosmap_std);

	}
}

int	copyout_nosmap_std(const void *kaddr, void *udaddr, size_t len);
int	copyout_smap_std(const void *kaddr, void *udaddr, size_t len);
int	copyout_nosmap_erms(const void *kaddr, void *udaddr, size_t len);
int	copyout_smap_erms(const void *kaddr, void *udaddr, size_t len);
DEFINE_IFUNC(, int, copyout, (const void *, void *, size_t), static)
{

	switch (cpu_stdext_feature & (CPUID_STDEXT_SMAP | CPUID_STDEXT_ERMS)) {
	case CPUID_STDEXT_SMAP:
		return (copyout_smap_std);
	case CPUID_STDEXT_ERMS:
		return (copyout_nosmap_erms);
	case CPUID_STDEXT_SMAP | CPUID_STDEXT_ERMS:
		return (copyout_smap_erms);
	default:
		return (copyout_nosmap_std);
	}
}
