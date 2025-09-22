/*	$OpenBSD: intr.h,v 1.16 2025/05/10 10:11:02 visa Exp $	*/
/*	$NetBSD: intr.h,v 1.12 2003/06/16 20:00:59 thorpej Exp $	*/

/*
 * Copyright (c) 2001, 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_MACHINE_INTR_H_
#define	_MACHINE_INTR_H_

#ifdef _KERNEL

/* Interrupt priority `levels'; not mutually exclusive. */
#define	IPL_NONE	0	/* nothing */
#define	IPL_SOFTCLOCK	2	/* soft clock interrupts */
#define	IPL_SOFTNET	3	/* soft network interrupts */
#define	IPL_SOFTTTY	4	/* soft terminal interrupts */
#define	IPL_BIO		5	/* block I/O */
#define	IPL_NET		6	/* network */
#define	IPL_TTY		7	/* terminal */
#define	IPL_VM		8	/* memory allocation */
#define	IPL_AUDIO	9	/* audio */
#define	IPL_CLOCK	10	/* clock */
#define	IPL_SCHED	IPL_CLOCK
#define	IPL_STATCLOCK	IPL_CLOCK
#define	IPL_HIGH	11	/* everything */
#define	IPL_IPI		12	/* interprocessor interrupt */
#define	NIPL		13	/* number of levels */

#define	IPL_MPFLOOR	IPL_TTY
/* Interrupt priority 'flags'. */
#define	IPL_IRQMASK	0xf	/* priority only */
#define	IPL_FLAGMASK	0xf00	/* flags only*/
#define	IPL_MPSAFE	0x100	/* 'mpsafe' interrupt, no kernel lock */

/* Interrupt sharing types. */
#define	IST_NONE	0	/* none */
#define	IST_PULSE	1	/* pulsed */
#define	IST_EDGE	2	/* edge-triggered */
#define	IST_LEVEL	3	/* level-triggered */

#define	IST_LEVEL_LOW		IST_LEVEL
#define	IST_LEVEL_HIGH		4
#define	IST_EDGE_FALLING	IST_EDGE
#define	IST_EDGE_RISING		5
#define	IST_EDGE_BOTH		6

#define __USE_MI_SOFTINTR

#include <sys/softintr.h>

#ifndef _LOCORE
#include <sys/queue.h>

struct cpu_info;

int     splraise(int);
int     spllower(int);
void    splx(int);

void	arm_do_pending_intr(int);
void	arm_set_intr_handler(int (*raise)(int), int (*lower)(int),
	void (*x)(int), void (*setipl)(int),
	void *(*intr_establish)(int irqno, int level, struct cpu_info *ci,
	    int (*func)(void *), void *cookie, char *name),
	void (*intr_disestablish)(void *cookie),
	const char *(*intr_string)(void *cookie),
	void (*intr_handle)(void *));

struct arm_intr_func {
	int (*raise)(int);
	int (*lower)(int);
	void (*x)(int);
	void (*setipl)(int);
	void *(*intr_establish)(int irqno, int level, struct cpu_info *,
	    int (*func)(void *), void *cookie, char *name);
	void (*intr_disestablish)(void *cookie);
	const char *(*intr_string)(void *cookie);
};

extern struct arm_intr_func arm_intr_func;

#define splraise(cpl)		(arm_intr_func.raise(cpl))
#define _splraise(cpl)		(arm_intr_func.raise(cpl))
#define spllower(cpl)		(arm_intr_func.lower(cpl))
#define splx(cpl)		(arm_intr_func.x(cpl))

#define	splhigh()	splraise(IPL_HIGH)
#define	splsoftclock()	splraise(IPL_SOFTCLOCK)
#define	splsoftnet()	splraise(IPL_SOFTNET)
#define	splbio()	splraise(IPL_BIO)
#define	splnet()	splraise(IPL_NET)
#define	spltty()	splraise(IPL_TTY)
#define	splvm()		splraise(IPL_VM)
#define	splaudio()	splraise(IPL_AUDIO)
#define	splclock()	splraise(IPL_CLOCK)
#define	splstatclock()	splraise(IPL_STATCLOCK)

#define	spl0()		spllower(IPL_NONE)

#define	splsched()	splhigh()

void	intr_barrier(void *);

void arm_init_smask(void); /* XXX */
extern uint32_t arm_smask[NIPL];
void arm_setsoftintr(int si);

#define softintr arm_setsoftintr

void *arm_intr_establish(int irqno, int level, int (*func)(void *),
    void *cookie, char *name);
void arm_intr_disestablish(void *cookie);
const char *arm_intr_string(void *cookie);

/* XXX - this is probably the wrong location for this */
void arm_clock_register(void (*)(void), void (*)(u_int), void (*)(int),
    void (*)(void));

struct interrupt_controller {
	int	ic_node;
	void	*ic_cookie;
	void	*(*ic_establish)(void *, int *, int, struct cpu_info *,
		    int (*)(void *), void *, char *);
	void	*(*ic_establish_msi)(void *, uint64_t *, uint64_t *, int,
		    struct cpu_info *, int (*)(void *), void *, char *);
	void	 (*ic_disestablish)(void *);
	void	 (*ic_enable)(void *);
	void	 (*ic_disable)(void *);
	void	 (*ic_route)(void *, int, struct cpu_info *);
	void	 (*ic_cpu_enable)(void);
	void	 (*ic_barrier)(void *);

	LIST_ENTRY(interrupt_controller) ic_list;
	uint32_t ic_phandle;
	uint32_t ic_cells;
};

void	 arm_intr_init_fdt(void);
void	 arm_intr_register_fdt(struct interrupt_controller *);
void	*arm_intr_establish_fdt(int, int, int (*)(void *),
	    void *, char *);
void	*arm_intr_establish_fdt_cpu(int, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	*arm_intr_establish_fdt_idx(int, int, int, int (*)(void *),
	    void *, char *);
void	*arm_intr_establish_fdt_idx_cpu(int, int, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	*arm_intr_establish_fdt_imap(int, int *, int, int, int (*)(void *),
	    void *, char *);
void	*arm_intr_establish_fdt_imap_cpu(int, int *, int, int,
	    struct cpu_info *, int (*)(void *), void *, char *);
void	*arm_intr_establish_fdt_msi(int, uint64_t *, uint64_t *, int,
	    int (*)(void *), void *, char *);
void	*arm_intr_establish_fdt_msi_cpu(int, uint64_t *, uint64_t *, int,
	    struct cpu_info *, int (*)(void *), void *, char *);
void	 arm_intr_disestablish_fdt(void *);
void	 arm_intr_enable(void *);
void	 arm_intr_disable(void *);
void	 arm_intr_route(void *, int, struct cpu_info *);
void	 arm_intr_cpu_enable(void);

void	*arm_intr_parent_establish_fdt(void *, int *, int,
	    struct cpu_info *ci, int (*)(void *), void *, char *);
void	 arm_intr_parent_disestablish_fdt(void *);

void	 arm_send_ipi(struct cpu_info *, int);
extern void (*intr_send_ipi_func)(struct cpu_info *, int);

#define ARM_IPI_NOP	0
#define ARM_IPI_DDB	1

#ifdef DIAGNOSTIC
/*
 * Although this function is implemented in MI code, it must be in this MD
 * header because we don't want this header to include MI includes.
 */
void splassert_fail(int, int, const char *);
extern int splassert_ctl;
void arm_splassert_check(int, const char *);
#define splassert(__wantipl) do {                               \
	if (splassert_ctl > 0) {                                \
		arm_splassert_check(__wantipl, __func__);    \
	}                                                       \
} while (0)
#define splsoftassert(wantipl) splassert(wantipl)
#else
#define splassert(wantipl)      do { /* nothing */ } while (0)
#define splsoftassert(wantipl)  do { /* nothing */ } while (0)
#endif

#endif /* ! _LOCORE */

#define ARM_IRQ_HANDLER arm_intr

#endif /* _KERNEL */

#endif	/* _MACHINE_INTR_H_ */

