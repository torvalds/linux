/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2004 by Peter Grehan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Machine-dependent thread prototypes/definitions.
 */
#ifndef _PTHREAD_MD_H_
#define	_PTHREAD_MD_H_

#include <stddef.h>
#include <sys/types.h>

#define	CPU_SPINWAIT

#define	DTV_OFFSET		offsetof(struct tcb, tcb_dtv)
#ifdef __powerpc64__
#define	TP_OFFSET		0x7010
#else
#define	TP_OFFSET		0x7008
#endif

/*
 * Variant I tcb. The structure layout is fixed, don't blindly
 * change it.
 * %r2 (32-bit) or %r13 (64-bit) points to end of the structure.
 */
struct tcb {
	void			*tcb_dtv;
	struct pthread		*tcb_thread;
};

static __inline void
_tcb_set(struct tcb *tcb)
{
#ifdef __powerpc64__
	__asm __volatile("mr 13,%0" ::
	    "r"((uint8_t *)tcb + TP_OFFSET));
#else
	__asm __volatile("mr 2,%0" ::
	    "r"((uint8_t *)tcb + TP_OFFSET));
#endif
}

static __inline struct tcb *
_tcb_get(void)
{
        register struct tcb *tcb;

#ifdef __powerpc64__
	__asm __volatile("addi %0,13,%1" : "=r"(tcb) : "i"(-TP_OFFSET));
#else
	__asm __volatile("addi %0,2,%1" : "=r"(tcb) : "i"(-TP_OFFSET));
#endif

	return (tcb);
}

static __inline struct pthread *
_get_curthread(void)
{
	if (_thr_initial)
		return (_tcb_get()->tcb_thread);
	return (NULL);
}

#endif /* _PTHREAD_MD_H_ */
