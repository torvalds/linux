/*	$OpenBSD: intr.h,v 1.26 2025/05/11 19:41:05 miod Exp $	*/
/*	$NetBSD: intr.h,v 1.8 2001/01/14 23:50:30 thorpej Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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

#ifndef _MACHINE_INTR_H_
#define _MACHINE_INTR_H_

#include <sparc64/sparc64/intreg.h>

#include <sys/evcount.h>

/*
 * Interrupt handler chains.  Interrupt handlers should return 0 for
 * ``not me'' or 1 (``I took care of it'').  intr_establish() inserts a
 * handler into the list.  The handler is called with its (single)
 * argument, or with a pointer to a clockframe if ih_arg is NULL.
 */
struct intrhand {
	int			(*ih_fun)(void *);
	void			*ih_arg;
	short			ih_number;	/* interrupt number */
						/* the H/W provides */
	char			ih_pil;		/* interrupt priority */
	char			ih_mpsafe;
	struct intrhand		*ih_next;	/* global list */
	struct intrhand		*ih_pending;	/* pending list */
	volatile u_int64_t	*ih_map;	/* interrupt map reg */
	volatile u_int64_t	*ih_clr;	/* clear interrupt reg */
	void			(*ih_ack)(struct intrhand *);
	struct evcount		ih_count;	/* # of interrupts */
	const void		*ih_bus;	/* parent bus */
	struct cpu_info		*ih_cpu;	/* target */
	char			ih_name[32];	/* device name */
};

extern struct intrhand *intrlev[MAXINTNUM];

void    intr_establish(struct intrhand *);

/* XXX - arbitrary numbers; no interpretation is defined yet */
#define	IPL_NONE	0		/* nothing */
#define	IPL_SOFTINT	1		/* softint */
#define	IPL_SOFTCLOCK	1		/* timeouts */
#define	IPL_SOFTNET	2		/* protocol stack */
#define	IPL_BIO		PIL_BIO		/* block I/O */
#define	IPL_NET		PIL_NET		/* network */
#define	IPL_SOFTTTY	4		/* delayed terminal handling */
#define	IPL_TTY		PIL_TTY		/* terminal */
#define	IPL_VM		PIL_VM		/* memory allocation */
#define	IPL_AUDIO	PIL_AUD		/* audio */
#define	IPL_CLOCK	PIL_CLOCK	/* clock */
#define	IPL_SERIAL	PIL_SER		/* serial */
#define	IPL_SCHED	PIL_SCHED	/* scheduler */
#define	IPL_STATCLOCK	PIL_STATCLOCK	/* statclock */
#define	IPL_HIGH	PIL_HIGH	/* everything */

#define spl0()		_spl(IPL_NONE)
#define splsoftclock()	_splraise(IPL_SOFTCLOCK)
#define splsoftnet()	_splraise(IPL_SOFTNET)
#define splbio()	_splraise(IPL_BIO)
#define splnet()	_splraise(IPL_NET)
#define splsofttty()	_splraise(IPL_SOFTTTY)
#define spltty()	_splraise(IPL_TTY)
#define splvm()		_splraise(IPL_VM)
#define splaudio()	_splraise(IPL_AUDIO)
#define splclock()	_splraise(IPL_CLOCK)
#define splserial()	_splraise(IPL_SERIAL)
#define splsched()	_splraise(IPL_SCHED)
#define splstatclock()	_splraise(IPL_STATCLOCK)
#define splhigh()	_splraise(IPL_HIGH)
#define splx(_oldipl)	_splx(_oldipl)

#define splzs()		splserial()

#define	IPL_MPFLOOR	IPL_SERIAL
#define	IPL_MPSAFE	0x100

int	 splraise(int);
void	 intr_barrier(void *);

void	*softintr_establish(int, void (*)(void *), void *);
void	 softintr_disestablish(void *);
void	 softintr_schedule(void *);

void	*softintr_establish_raw(int, void (*)(void *), void *);
#define	softintr_disestablish_raw(ih)	softintr_disestablish(ih)
#define	softintr_schedule_raw(ih)	softintr_schedule(ih)

void	send_softint(int, struct intrhand *);

#endif /* _MACHINE_INTR_H_ */
