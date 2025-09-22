/*	$OpenBSD: intr.h,v 1.36 2025/06/11 09:57:01 kettenis Exp $	*/
/*	$NetBSD: intr.h,v 1.2 2003/05/04 22:01:56 fvdl Exp $	*/

/*-
 * Copyright (c) 1998, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum, and by Jason R. Thorpe.
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

#define __USE_MI_SOFTINTR

#include <sys/softintr.h>
#include <machine/intrdefs.h>

#ifndef _LOCORE
#include <machine/cpu.h>

#include <sys/evcount.h>

/*
 * Struct describing an interrupt source for a CPU. struct cpu_info
 * has an array of MAX_INTR_SOURCES of these. The index in the array
 * is equal to the stub number of the stubcode as present in vector.s
 *
 * The primary CPU's array of interrupt sources has its first 16
 * entries reserved for legacy ISA irq handlers. This means that
 * they have a 1:1 mapping for arrayindex:irq_num. This is not
 * true for interrupts that come in through IO APICs, to find
 * their source, go through ci->ci_isources[index].is_pic
 *
 * It's possible to always maintain a 1:1 mapping, but that means
 * limiting the total number of interrupt sources to MAX_INTR_SOURCES
 * (32), instead of 32 per CPU. It also would mean that having multiple
 * IO APICs which deliver interrupts from an equal pin number would
 * overlap if they were to be sent to the same CPU.
 */

struct intrstub {
	void *ist_entry;
	void *ist_recurse;
	void *ist_resume;
};

struct intrsource {
	int is_maxlevel;		/* max. IPL for this source */
	int is_pin;			/* IRQ for legacy; pin for IO APIC */
	struct intrhand *is_handlers;	/* handler chain */
	struct pic *is_pic;		/* originating PIC */
	void *is_recurse;		/* entry for spllower */
	void *is_resume;		/* entry for doreti */
	char is_evname[32];		/* event counter name */
	int is_flags;			/* see below */
	int is_type;			/* level, edge */
	int is_idtvec;
	int is_minlevel;
};

#define IS_LEGACY	0x0001		/* legacy ISA irq source */
#define IS_IPI		0x0002
#define IS_LOG		0x0004


/*
 * Interrupt handler chains.  *_intr_establish() insert a handler into
 * the list.  The handler is called with its (single) argument.
 */

struct intrhand {
	int	(*ih_fun)(void *);
	void	*ih_arg;
	int	ih_level;
	int	ih_flags;
	struct	intrhand *ih_next;
	int	ih_pin;
	int	ih_slot;
	struct cpu_info *ih_cpu;
	int	ih_irq;
	struct evcount ih_count;
};

#define IMASK(ci,level) (ci)->ci_imask[(level)]
#define IUNMASK(ci,level) (ci)->ci_iunmask[(level)]

extern void Xspllower(int);

int splraise(int);
int spllower(int);
void softintr(int);
void dosoftint(int);

/*
 * Convert spl level to local APIC level
 */
#define APIC_LEVEL(l)   ((l) << 4)

/*
 * compiler barrier: prevent reordering of instructions.
 * This prevents the compiler from reordering code around
 * this "instruction", acting as a sequence point for code generation.
 */

#define	__splbarrier() __asm volatile("":::"memory")

/*
 * Hardware interrupt masks
 */
#define	splbio()	splraise(IPL_BIO)
#define	splnet()	splraise(IPL_NET)
#define	spltty()	splraise(IPL_TTY)
#define	splaudio()	splraise(IPL_AUDIO)
#define	splclock()	splraise(IPL_CLOCK)
#define	splstatclock()	splclock()
#define splipi()	splraise(IPL_IPI)

/*
 * Software interrupt masks
 */
#define	splsoftclock()	splraise(IPL_SOFTCLOCK)
#define	splsoftnet()	splraise(IPL_SOFTNET)
#define	splsofttty()	splraise(IPL_SOFTTTY)

/*
 * Miscellaneous
 */
#define	splvm()		splraise(IPL_VM)
#define	splhigh()	splraise(IPL_HIGH)
#define	spl0()		spllower(IPL_NONE)
#define	splsched()	splraise(IPL_SCHED)
#define	splx(x)		spllower(x)

/* SPL asserts */
#ifdef DIAGNOSTIC
/*
 * Although this function is implemented in MI code, it must be in this MD
 * header because we don't want this header to include MI includes.
 */
void splassert_fail(int, int, const char *);
extern int splassert_ctl;
void splassert_check(int, const char *);
#define splassert(__wantipl) do {			\
	if (splassert_ctl > 0) {			\
		splassert_check(__wantipl, __func__);	\
	}						\
} while (0)
#define splsoftassert(wantipl) splassert(wantipl)
#else
#define splassert(wantipl)	do { /* nada */ } while (0)
#define splsoftassert(wantipl)	do { /* nada */ } while (0)
#endif

#define IPLSHIFT 4			/* The upper nibble of vectors is the IPL.      */
#define IPL(level) ((level) >> IPLSHIFT)	/* Extract the IPL.    */

#include <machine/pic.h>

/*
 * Stub declarations.
 */

extern void Xsoftclock(void);
extern void Xsoftnet(void);
extern void Xsofttty(void);

extern struct intrstub i8259_stubs[];
extern struct intrstub ioapic_edge_stubs[];
extern struct intrstub ioapic_level_stubs[];

struct cpu_info;

extern int intr_shared_edge;

extern char idt_allocmap[];

void intr_default_setup(void);
int x86_nmi(void);
void intr_calculatemasks(struct cpu_info *);
int intr_allocate_slot_cpu(struct cpu_info *, struct pic *, int, int *);
int intr_allocate_slot(struct pic *, int, int, int, struct cpu_info **, int *,
	    int *);
void *intr_establish(int, struct pic *, int, int, int,
	    struct cpu_info *, int (*)(void *), void *, const char *);
void intr_disestablish(struct intrhand *);
int intr_handler(struct intrframe *, struct intrhand *);
void cpu_intr_init(struct cpu_info *);
void intr_printconfig(void);
void intr_barrier(void *);
void intr_set_wakeup(void *);
void intr_enable_wakeup(void);
void intr_disable_wakeup(void);

#ifdef MULTIPROCESSOR
void x86_send_ipi(struct cpu_info *, int);
int x86_fast_ipi(struct cpu_info *, int);
void x86_broadcast_ipi(int);
void x86_ipi_handler(void);
void x86_setperf_ipi(struct cpu_info *);

extern void (*ipifunc[X86_NIPI])(struct cpu_info *);
#endif

#endif /* !_LOCORE */

#endif /* !_MACHINE_INTR_H_ */
