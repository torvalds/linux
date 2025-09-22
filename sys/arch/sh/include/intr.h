/*	$OpenBSD: intr.h,v 1.11 2025/04/30 12:30:54 visa Exp $	*/
/*	$NetBSD: intr.h,v 1.22 2006/01/24 23:51:42 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
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

#ifndef _SH_INTR_H_
#define	_SH_INTR_H_

#ifdef	_KERNEL

#define __USE_MI_SOFTINTR

#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/softintr.h>
#include <sh/psl.h>

/* Interrupt sharing types. */
#define	IST_NONE		0	/* none */
#define	IST_PULSE		1	/* pulsed */
#define	IST_EDGE		2	/* edge-triggered */
#define	IST_LEVEL		3	/* level-triggered */

/* Interrupt priority levels */
#define	_IPL_N		15
#define	_IPL_NSOFT	4

#define	IPL_NONE	0	/* nothing */
#define	IPL_SOFTCLOCK	2	/* timeouts */
#define	IPL_SOFTNET	3	/* protocol stacks */
#define	IPL_SOFTSERIAL	4	/* serial */
#define	IPL_SOFTTTY	IPL_SOFTSERIAL

struct intc_intrhand {
	int	(*ih_func)(void *);
	void	*ih_arg;
	int	ih_level;	/* SR.I[0:3] value */
	int	ih_evtcode;	/* INTEVT or INTEVT2(SH7709/SH7709A) */
	int	ih_idx;		/* evtcode -> intrhand mapping */
	int	ih_irq;
	struct evcount ih_count;
	const char *ih_name;
};

void intr_barrier(void *);

/* from 0x200 by 0x20 -> from 0 by 1 */
#define	EVTCODE_TO_MAP_INDEX(x)		(((x) >> 5) - 0x10)
#define	EVTCODE_TO_IH_INDEX(x)						\
	__intc_evtcode_to_ih[EVTCODE_TO_MAP_INDEX(x)]
#define	EVTCODE_IH(x)	(&__intc_intrhand[EVTCODE_TO_IH_INDEX(x)])
extern int8_t __intc_evtcode_to_ih[];
extern struct intc_intrhand __intc_intrhand[];

void intc_init(void);
void *intc_intr_establish(int, int, int, int (*)(void *), void *, const char *);
void intc_intr_disestablish(void *);
void intc_intr_enable(int);
void intc_intr_disable(int);
void intc_intr(int, int, int);

void intpri_intr_priority(int evtcode, int level);

void softintr(int);

#endif	/* _KERNEL */

#endif /* !_SH_INTR_H_ */
