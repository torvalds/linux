/*	$OpenBSD: intr.h,v 1.23 2025/05/10 10:01:03 visa Exp $ */

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

#ifndef _MACHINE_INTR_H_
#define _MACHINE_INTR_H_

#define __USE_MI_SOFTINTR

#include <sys/softintr.h>

/*
 * The interrupt level ipl is a logical level; per-platform interrupt
 * code will turn it into the appropriate hardware interrupt masks
 * values.
 *
 * Interrupt sources on the CPU are kept enabled regardless of the
 * current ipl value; individual hardware sources interrupting while
 * logically masked are masked on the fly, remembered as pending, and
 * unmasked at the first splx() opportunity.
 *
 * An exception to this rule is the clock interrupt. Clock interrupts
 * are always allowed to happen, but will (of course!) not be serviced
 * if logically masked.  The reason for this is that clocks usually sit on
 * INT5 and cannot be easily masked if external hardware masking is used.
 */

/* Interrupt priority `levels'; not mutually exclusive. */
#define	IPL_NONE	0	/* nothing */
#define	IPL_SOFTINT	1	/* soft interrupts */
#define	IPL_SOFTCLOCK	1	/* soft clock interrupts */
#define	IPL_SOFTNET	2	/* soft network interrupts */
#define	IPL_SOFTTTY	3	/* soft terminal interrupts */
#define	IPL_SOFTHIGH	IPL_SOFTTTY	/* highest level of soft interrupts */
#define	IPL_BIO		4	/* block I/O */
#define	IPL_AUDIO	IPL_BIO
#define	IPL_NET		5	/* network */
#define	IPL_TTY		6	/* terminal */
#define	IPL_VM		7	/* memory allocation */
#define	IPL_CLOCK	8	/* clock */
#define	IPL_SCHED	IPL_CLOCK
#define	IPL_HIGH	9	/* everything */
#define	IPL_IPI		10	/* interprocessor interrupt */
#define	NIPLS		11	/* number of levels */

#define IPL_MPFLOOR	IPL_TTY

/* Interrupt priority 'flags'. */
#define	IPL_MPSAFE	0x100

/* Interrupt sharing types. */
#define	IST_NONE	0	/* none */
#define	IST_PULSE	1	/* pulsed */
#define	IST_EDGE	2	/* edge-triggered */
#define	IST_LEVEL	3	/* level-triggered */

#ifndef _LOCORE

void	 softintr(int);

#define splbio()	splraise(IPL_BIO)
#define splnet()	splraise(IPL_NET)
#define spltty()	splraise(IPL_TTY)
#define splaudio()	splraise(IPL_AUDIO)
#define splvm()		splraise(IPL_VM)
#define splclock()	splraise(IPL_CLOCK)
#define splsched()	splraise(IPL_SCHED)
#define splhigh()	splraise(IPL_HIGH)

#define splsoftclock()	splraise(IPL_SOFTCLOCK)
#define splsoftnet()	splraise(IPL_SOFTNET)
#define splstatclock()	splhigh()

#define spl0()		spllower(0)

void	splinit(void);

#ifdef DIAGNOSTIC
/*
 * Although this function is implemented in MI code, it must be in this MD
 * header because we don't want this header to include MI includes.
 */
void splassert_fail(int, int, const char *);
extern int splassert_ctl;
void splassert_check(int, const char *);
#define	splassert(__wantipl) do {				\
	if (splassert_ctl > 0) {				\
		splassert_check(__wantipl, __func__);		\
	}							\
} while (0)
#define	splsoftassert(wantipl)	splassert(wantipl)
#else
#define	splassert(X)
#define	splsoftassert(X)
#endif

void	register_splx_handler(void (*)(int));
int	splraise(int);
void	splx(int);
int	spllower(int);

void	intr_barrier(void *);

/*
 * Low level interrupt dispatcher registration data.
 */

/* Schedule priorities for base interrupts (CPU) */
#define	INTPRI_IPI	0
#define	INTPRI_CLOCK	1
/* other values are system-specific */

#define NLOWINT	16		/* Number of low level registrations possible */

extern uint32_t idle_mask;

struct trapframe;
void	set_intr(int, uint32_t, uint32_t(*)(uint32_t, struct trapframe *));

uint32_t updateimask(uint32_t);
void	dosoftint(void);

struct intr_controller {
	void	  *ic_cookie;
	void	 (*ic_init)(void);
	void	*(*ic_establish)(int, int, int (*)(void *), void *,
		    const char *);
	void	*(*ic_establish_fdt_idx)(void *, int, int, int,
		    int (*)(void *), void *, const char *);
	void	 (*ic_disestablish)(void *);
	void	 (*ic_intr_barrier)(void *);

#ifdef MULTIPROCESSOR
	int	 (*ic_ipi_establish)(int (*)(void *), cpuid_t);
	void	 (*ic_ipi_set)(cpuid_t);
	void	 (*ic_ipi_clear)(cpuid_t);
#endif /* MULTIPROCESSOR */

	int	   ic_node;
	int	   ic_phandle;
	LIST_ENTRY(intr_controller) ic_list;
};

#ifdef MULTIPROCESSOR
#define ENABLEIPI() updateimask(~CR_INT_1) /* enable IPI interrupt level */
#endif

void   *octeon_intr_establish(int, int, int (*)(void *),
	    void *, const char *);
void	octeon_intr_disestablish(void *);
void	octeon_intr_init(void);
void	octeon_intr_register(struct intr_controller *);

void	*octeon_intr_establish_fdt(int, int, int (*)(void *),
	    void *, const char *);
void	*octeon_intr_establish_fdt_idx(int, int, int, int (*)(void *),
	    void *, const char *);
void	 octeon_intr_disestablish_fdt(void *);

#endif /* _LOCORE */

#endif /* _MACHINE_INTR_H_ */
