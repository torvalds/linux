/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * from: src/lib/libthr/arch/arm/include/pthread_md.h,v 1.3 2005/10/29 13:40:31 davidxu
 * $FreeBSD$
 */

/*
 * Machine-dependent thread prototypes/definitions.
 */
#ifndef _PTHREAD_MD_H_
#define	_PTHREAD_MD_H_

#include <sys/types.h>
#include <machine/sysarch.h>
#include <machine/tls.h>
#include <stddef.h>

#define	CPU_SPINWAIT
#define	DTV_OFFSET		offsetof(struct tcb, tcb_dtv)

/*
 * Variant I tcb. The structure layout is fixed, don't blindly
 * change it!
 */
struct tcb {
	void			*tcb_dtv;
	struct pthread		*tcb_thread;
};

/* Called from the thread to set its private data. */
static __inline void
_tcb_set(struct tcb *tcb)
{

	sysarch(MIPS_SET_TLS, tcb);
}

/*
 * Get the current tcb.
 */
#ifdef TLS_USE_SYSARCH
static __inline struct tcb *
_tcb_get(void)
{
	struct tcb *tcb;

	sysarch(MIPS_GET_TLS, &tcb);
	return tcb;
}

#else /* ! TLS_USE_SYSARCH */

#  if defined(__mips_n64)
static __inline struct tcb *
_tcb_get(void)
{
	uint64_t _rv;

	__asm__ __volatile__ (
	    ".set\tpush\n\t"
	    ".set\tmips64r2\n\t"
	    "rdhwr\t%0, $29\n\t"
	    ".set\tpop"
	    : "=r" (_rv));

	/*
	 * XXXSS See 'git show c6be4f4d2d1b71c04de5d3bbb6933ce2dbcdb317'
	 *
	 * Remove the offset since this really a request to get the TLS
	 * pointer via sysarch() (in theory).  Of course, this may go away
	 * once the TLS code is rewritten.
	 */
	return (struct tcb *)(_rv - TLS_TP_OFFSET - TLS_TCB_SIZE);
}
#  else /* mips 32 */
static __inline struct tcb *
_tcb_get(void)
{
	uint32_t _rv;

	__asm__ __volatile__ (
	    ".set\tpush\n\t"
	    ".set\tmips32r2\n\t"
	    "rdhwr\t%0, $29\n\t"
	    ".set\tpop"
	    : "=r" (_rv));

	/*
	 * XXXSS See 'git show c6be4f4d2d1b71c04de5d3bbb6933ce2dbcdb317'
	 *
	 * Remove the offset since this really a request to get the TLS
	 * pointer via sysarch() (in theory).  Of course, this may go away
	 * once the TLS code is rewritten.
	 */
	return (struct tcb *)(_rv - TLS_TP_OFFSET - TLS_TCB_SIZE);
}
#  endif /* ! __mips_n64 */
#endif /* ! TLS_USE_SYSARCH */

static __inline struct pthread *
_get_curthread(void)
{
	if (_thr_initial)
		return (_tcb_get()->tcb_thread);
	return (NULL);
}

#endif /* _PTHREAD_MD_H_ */
