/*	$OpenBSD: intr.h,v 1.25 2025/06/30 14:19:20 kettenis Exp $ */

/*
 * Copyright (c) 2001-2004 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef	_MACHINE_INTR_H_
#define	_MACHINE_INTR_H_

/*
 * The interrupt level ipl is a logical level; per-platform interrupt
 * code will turn it into the appropriate hardware interrupt masks
 * values.
 *
 * Interrupt sources on the CPU are kept enabled regardless of the
 * current ipl value; individual hardware sources interrupting while
 * logically masked are masked on the fly, remembered as pending, and
 * unmasked at the first splx() opportunity.
 */
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
#define	IPL_WAKEUP	0x200	/* 'wakeup' interrupt */

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

void	softintr(int);

int	splraise(int);
int	spllower(int);
void	splx(int);

void	arm_do_pending_intr(int);
void	arm_set_intr_handler(int (*)(int), int (*)(int), void (*)(int),
	    void (*)(int), void (*)(void *), void (*)(void *),
	    void (*)(void), void (*)(void));

struct machine_intr_handle {
	struct interrupt_controller *ih_ic;
	void *ih_ih;
};

struct arm_intr_func {
	int (*raise)(int);
	int (*lower)(int);
	void (*x)(int);
	void (*setipl)(int);
	void (*enable_wakeup)(void);
	void (*disable_wakeup)(void);
};

extern struct arm_intr_func arm_intr_func;

#define	splraise(cpl)		(arm_intr_func.raise(cpl))
#define	_splraise(cpl)		(arm_intr_func.raise(cpl))
#define	spllower(cpl)		(arm_intr_func.lower(cpl))
#define	splx(cpl)		(arm_intr_func.x(cpl))

#define	splsoftclock()	splraise(IPL_SOFTCLOCK)
#define	splsoftnet()	splraise(IPL_SOFTNET)
#define	splsofttty()	splraise(IPL_SOFTTTY)
#define	splbio()	splraise(IPL_BIO)
#define	splnet()	splraise(IPL_NET)
#define	spltty()	splraise(IPL_TTY)
#define	splvm()		splraise(IPL_VM)
#define	splaudio()	splraise(IPL_AUDIO)
#define	splclock()	splraise(IPL_CLOCK)
#define	splsched()	splraise(IPL_SCHED)
#define	splstatclock()	splraise(IPL_STATCLOCK)
#define	splhigh()	splraise(IPL_HIGH)

#define	spl0()		spllower(IPL_NONE)

void	 intr_barrier(void *);
void	 intr_set_wakeup(void *);
void	 intr_enable_wakeup(void);
void	 intr_disable_wakeup(void);

void	 arm_init_smask(void); /* XXX */
extern uint32_t arm_smask[NIPL];

/* XXX - this is probably the wrong location for this */
void arm_clock_register(void (*)(void), void (*)(u_int), void (*)(int),
    void (*)(void));

struct cpu_info;

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
	void	 (*ic_set_wakeup)(void *);

	LIST_ENTRY(interrupt_controller) ic_list;
	uint32_t ic_phandle;
	uint32_t ic_cells;
	uint32_t ic_gic_its_id;
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
#define ARM_IPI_HALT	2

#ifdef DIAGNOSTIC
/*
 * Although this function is implemented in MI code, it must be in this MD
 * header because we don't want this header to include MI includes.
 */
void splassert_fail(int, int, const char *);
extern int splassert_ctl;
void arm_splassert_check(int, const char *);
#define splassert(__wantipl) do {				\
	if (splassert_ctl > 0) {				\
		arm_splassert_check(__wantipl, __func__);	\
	}							\
} while (0)
#define	splsoftassert(wantipl)	splassert(wantipl)
#else
#define	splassert(wantipl)	do { /* nothing */ } while (0)
#define	splsoftassert(wantipl)	do { /* nothing */ } while (0)
#endif

#endif /* ! _LOCORE */

#endif /* _KERNEL */

#endif	/* _MACHINE_INTR_H_ */

