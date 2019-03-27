/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Doug Rabson
 * All rights reserved.
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
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_CPUFUNC_H_
#define	_MACHINE_CPUFUNC_H_

#ifdef _KERNEL

#include <sys/types.h>

#include <machine/psl.h>
#include <machine/spr.h>

struct thread;

#ifdef KDB
void breakpoint(void);
#else
static __inline void
breakpoint(void)
{

	return;
}
#endif

/* CPU register mangling inlines */

static __inline void
mtmsr(register_t value)
{

	__asm __volatile ("mtmsr %0; isync" :: "r"(value));
}

#ifdef __powerpc64__
static __inline void
mtmsrd(register_t value)
{

	__asm __volatile ("mtmsrd %0; isync" :: "r"(value));
}
#endif

static __inline register_t
mfmsr(void)
{
	register_t value;

	__asm __volatile ("mfmsr %0" : "=r"(value));

	return (value);
}

#ifndef __powerpc64__
static __inline void
mtsrin(vm_offset_t va, register_t value)
{

	__asm __volatile ("mtsrin %0,%1; isync" :: "r"(value), "r"(va));
}

static __inline register_t
mfsrin(vm_offset_t va)
{
	register_t value;

	__asm __volatile ("mfsrin %0,%1" : "=r"(value) : "r"(va));

	return (value);
}
#endif

static __inline register_t
mfctrl(void)
{
	register_t value;

	__asm __volatile ("mfspr %0,136" : "=r"(value));

	return (value);
}


static __inline void
mtdec(register_t value)
{

	__asm __volatile ("mtdec %0" :: "r"(value));
}

static __inline register_t
mfdec(void)
{
	register_t value;

	__asm __volatile ("mfdec %0" : "=r"(value));

	return (value);
}

static __inline register_t
mfpvr(void)
{
	register_t value;

	__asm __volatile ("mfpvr %0" : "=r"(value));

	return (value);
}

static __inline u_quad_t
mftb(void)
{
	u_quad_t tb;
      #ifdef __powerpc64__
	__asm __volatile ("mftb %0" : "=r"(tb));
      #else
	uint32_t *tbup = (uint32_t *)&tb;
	uint32_t *tblp = tbup + 1;

	do {
		*tbup = mfspr(TBR_TBU);
		*tblp = mfspr(TBR_TBL);
	} while (*tbup != mfspr(TBR_TBU));
      #endif

	return (tb);
}

static __inline void
mttb(u_quad_t time)
{

	mtspr(TBR_TBWL, 0);
	mtspr(TBR_TBWU, (uint32_t)(time >> 32));
	mtspr(TBR_TBWL, (uint32_t)(time & 0xffffffff));
}

static __inline void
eieio(void)
{

	__asm __volatile ("eieio" : : : "memory");
}

static __inline void
isync(void)
{

	__asm __volatile ("isync" : : : "memory");
}

static __inline void
powerpc_sync(void)
{

	__asm __volatile ("sync" : : : "memory");
}

static __inline register_t
intr_disable(void)
{
	register_t msr;

	msr = mfmsr();
	mtmsr(msr & ~PSL_EE);
	return (msr);
}

static __inline void
intr_restore(register_t msr)
{

	mtmsr(msr);
}

static __inline struct pcpu *
get_pcpu(void)
{
	struct pcpu *ret;

	__asm __volatile("mfsprg %0, 0" : "=r"(ret));

	return (ret);
}

#endif /* _KERNEL */

#endif /* !_MACHINE_CPUFUNC_H_ */
