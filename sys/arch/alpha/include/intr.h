/* $OpenBSD: intr.h,v 1.52 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: intr.h,v 1.26 2000/06/03 20:47:41 thorpej Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 */

/*
 * Copyright (c) 1997 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#ifndef _MACHINE_INTR_H_
#define _MACHINE_INTR_H_

#define __USE_MI_SOFTINTR

#include <sys/evcount.h>
#include <sys/softintr.h>
#include <machine/atomic.h>

/*
 * The Alpha System Control Block.  This is 8k long, and you get
 * 16 bytes per vector (i.e. the vector numbers are spaced 16
 * apart).
 *
 * This is sort of a "shadow" SCB -- rather than the CPU jumping
 * to (SCBaddr + (16 * vector)), like it does on the VAX, we get
 * a vector number in a1.  We use the SCB to look up a routine/arg
 * and jump to it.
 *
 * Since we use the SCB only for I/O interrupts, we make it shorter
 * than normal, starting it at vector 0x800 (the start of the I/O
 * interrupt vectors).
 */
#define	SCB_IOVECBASE	0x0800
#define	SCB_VECSIZE	0x0010
#define	SCB_SIZE	0x2000

#define	SCB_VECTOIDX(x)	((x) >> 4)
#define	SCB_IDXTOVEC(x)	((x) << 4)

#define	SCB_NIOVECS	SCB_VECTOIDX(SCB_SIZE - SCB_IOVECBASE)

struct scbvec {
	void	(*scb_func)(void *, u_long);
	void	*scb_arg;
};

/*
 * Alpha interrupts come in at one of 4 levels:
 *
 *	software interrupt level
 *	i/o level 1
 *	i/o level 2
 *	clock level
 *
 * However, since we do not have any way to know which hardware
 * level a particular i/o interrupt comes in on, we have to
 * whittle it down to 3.
 */

#define	IPL_NONE	ALPHA_PSL_IPL_0
#define	IPL_SOFTINT	ALPHA_PSL_IPL_SOFT
#define	IPL_BIO		ALPHA_PSL_IPL_IO
#define	IPL_NET		ALPHA_PSL_IPL_IO
#define	IPL_TTY		ALPHA_PSL_IPL_IO
#define	IPL_SERIAL	ALPHA_PSL_IPL_IO
#define	IPL_AUDIO	ALPHA_PSL_IPL_IO
#define	IPL_VM		ALPHA_PSL_IPL_IO
#define	IPL_CLOCK	ALPHA_PSL_IPL_CLOCK
#define	IPL_SCHED	ALPHA_PSL_IPL_CLOCK
#define	IPL_IPI		ALPHA_PSL_IPL_HIGH	/* occur on _CLOCK, though */
#define	IPL_HIGH	ALPHA_PSL_IPL_HIGH

#define	IPL_SOFTSERIAL	0	/* serial software interrupts */
#define	IPL_SOFTTTY	IPL_SOFTSERIAL
#define	IPL_SOFTCLOCK	1	/* clock software interrupts */
#define	IPL_SOFTNET	2	/* network software interrupts */

#define	IPL_MPFLOOR	IPL_AUDIO

#define	IPL_MPSAFE	0	/* no "mpsafe" interrupts */

#define	IST_UNUSABLE	-1	/* interrupt cannot be used */
#define	IST_NONE	0	/* none (dummy) */
#define	IST_PULSE	1	/* pulsed */
#define	IST_EDGE	2	/* edge-triggered */
#define	IST_LEVEL	3	/* level-triggered */

#ifdef	_KERNEL

void intr_barrier(void *);

/* SPL asserts */
#ifdef DIAGNOSTIC
/*
 * Although this function is implemented in MI code, it must be in this MD
 * header because we don't want this header to include MI includes.
 */
void splassert_fail(int, int, const char *);
extern int splassert_ctl;
void splassert_check(int, const char *);
#define	splassert(__wantipl)						\
	do {								\
		if (splassert_ctl > 0) {				\
			splassert_check(__wantipl, __func__);		\
		}							\
	} while (0)
#define	splsoftassert(wantipl)	splassert(IPL_SOFTINT)
#else
#define	splassert(wantipl)	do { /* nothing */ } while (0)
#define	splsoftassert(wantipl)	do { /* nothing */ } while (0)
#endif

/* IPL-lowering/restoring macros */
#define splx(s)								\
    ((s) == ALPHA_PSL_IPL_0 ? spl0() : (int)alpha_pal_swpipl(s))

/* IPL-raising functions/macros */
int splraise(int);
int spl0(void);

#define splsoft()		splraise(IPL_SOFTINT)
#define splsoftserial()		splsoft()
#define splsoftclock()		splsoft()
#define splsoftnet()		splsoft()
#define splnet()		splraise(IPL_NET)
#define splbio()		splraise(IPL_BIO)
#define spltty()		splraise(IPL_TTY)
#define splserial()		splraise(IPL_SERIAL)
#define splaudio()		splraise(IPL_AUDIO)
#define splvm()			splraise(IPL_VM)
#define splclock()		splraise(IPL_CLOCK)
#define splstatclock()		splraise(IPL_CLOCK)
#define splsched()		splraise(IPL_SCHED)
#define splipi()		splraise(IPL_IPI)
#define splhigh()		splraise(IPL_HIGH)

/*
 * Interprocessor interrupts.  In order how we want them processed.
 */
#define	ALPHA_IPI_HALT			(1UL << 0)
#define	ALPHA_IPI_SHOOTDOWN		(1UL << 1)
#define	ALPHA_IPI_IMB			(1UL << 2)
#define	ALPHA_IPI_AST			(1UL << 3)
#define	ALPHA_IPI_SYNCH_FPU		(1UL << 4)
#define	ALPHA_IPI_DISCARD_FPU		(1UL << 5)
#define	ALPHA_IPI_PAUSE			(1UL << 6)

#define	ALPHA_NIPIS		7	/* must not exceed 64 */

struct cpu_info;
struct trapframe;

void	alpha_ipi_process(struct cpu_info *, struct trapframe *);
#ifdef MP_LOCKDEBUG
void	alpha_ipi_process_with_frame(struct cpu_info *);
#else
#define	alpha_ipi_process_with_frame(ci) alpha_ipi_process((ci), NULL)
#endif
void	alpha_send_ipi(unsigned long, unsigned long);
void	alpha_broadcast_ipi(unsigned long);
void	alpha_multicast_ipi(unsigned long, unsigned long);

/*
 * Alpha shared-interrupt-line common code.
 */

struct alpha_shared_intrhand {
	TAILQ_ENTRY(alpha_shared_intrhand)
		ih_q;
	struct alpha_shared_intr *ih_intrhead;
	int	(*ih_fn)(void *);
	void	*ih_arg;
	int	ih_level;
	unsigned int ih_num;
	struct evcount ih_count;
};

struct alpha_shared_intr {
	TAILQ_HEAD(,alpha_shared_intrhand)
		intr_q;
	void	*intr_private;
	int	intr_sharetype;
	int	intr_dfltsharetype;
	int	intr_nstrays;
	int	intr_maxstrays;
};

#define	ALPHA_SHARED_INTR_DISABLE(asi, num)				\
	((asi)[num].intr_maxstrays != 0 &&				\
	 (asi)[num].intr_nstrays == (asi)[num].intr_maxstrays)

extern int	intr_shared_edge;

/*
 * simulated software interrupt register
 */
extern unsigned long ssir;

#define	softintr(x)	atomic_setbits_ulong(&ssir, 1 << (x))

void	dosoftint(void);

struct alpha_shared_intr *alpha_shared_intr_alloc(unsigned int);
int	alpha_shared_intr_dispatch(struct alpha_shared_intr *,
	    unsigned int);
void	*alpha_shared_intr_establish(struct alpha_shared_intr *,
	    unsigned int, int, int, int (*)(void *), void *, const char *);
void	alpha_shared_intr_disestablish(struct alpha_shared_intr *, void *);
int	alpha_shared_intr_get_sharetype(struct alpha_shared_intr *,
	    unsigned int);
int	alpha_shared_intr_isactive(struct alpha_shared_intr *,
	    unsigned int);
int	alpha_shared_intr_firstactive(struct alpha_shared_intr *,
	    unsigned int);
void	alpha_shared_intr_set_dfltsharetype(struct alpha_shared_intr *,
	    unsigned int, int);
void	alpha_shared_intr_set_maxstrays(struct alpha_shared_intr *,
	    unsigned int, int);
void	alpha_shared_intr_reset_strays(struct alpha_shared_intr *,
	    unsigned int);
void	alpha_shared_intr_stray(struct alpha_shared_intr *, unsigned int,
	    const char *);
void	alpha_shared_intr_set_private(struct alpha_shared_intr *,
	    unsigned int, void *);
void	*alpha_shared_intr_get_private(struct alpha_shared_intr *,
	    unsigned int);

extern struct scbvec scb_iovectab[];

void	scb_init(void);
void	scb_set(u_long, void (*)(void *, u_long), void *);
u_long	scb_alloc(void (*)(void *, u_long), void *);
void	scb_free(u_long);

#define	SCB_ALLOC_FAILED	((u_long) -1)

#endif /* _KERNEL */
#endif /* ! _MACHINE_INTR_H_ */
