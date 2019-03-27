/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2003 David Xu <davidxu@freebsd.org>
 * Copyright (c) 2001 Daniel Eischen <deischen@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

/*
 * Machine-dependent thread prototypes/definitions.
 */
#ifndef _PTHREAD_MD_H_
#define	_PTHREAD_MD_H_

#include <stddef.h>
#include <sys/types.h>
#include <machine/sysarch.h>

#define	CPU_SPINWAIT		__asm __volatile("pause")

#define	DTV_OFFSET		offsetof(struct tcb, tcb_dtv)

/*
 * Variant II tcb, first two members are required by rtld,
 * %fs points to the structure.
 */
struct tcb {
	struct tcb		*tcb_self;	/* required by rtld */
	void			*tcb_dtv;	/* required by rtld */
	struct pthread		*tcb_thread;
	void			*tcb_spare[1];
};

/*
 * Evaluates to the byte offset of the per-tcb variable name.
 */
#define	__tcb_offset(name)	__offsetof(struct tcb, name)

/*
 * Evaluates to the type of the per-tcb variable name.
 */
#define	__tcb_type(name)	__typeof(((struct tcb *)0)->name)

/*
 * Evaluates to the value of the per-tcb variable name.
 */
#define	TCB_GET64(name) ({					\
	__tcb_type(name) __result;				\
								\
	u_long __i;						\
	__asm __volatile("movq %%fs:%1, %0"			\
	    : "=r" (__i)					\
	    : "m" (*(volatile u_long *)(__tcb_offset(name))));  \
	__result = (__tcb_type(name))__i;			\
								\
	__result;						\
})

static __inline void
_tcb_set(struct tcb *tcb)
{
	amd64_set_fsbase(tcb);
}

static __inline struct tcb *
_tcb_get(void)
{
	return (TCB_GET64(tcb_self));
}

static __inline struct pthread *
_get_curthread(void)
{
	return (TCB_GET64(tcb_thread));
}

#define	HAS__UMTX_OP_ERR	1

#endif
