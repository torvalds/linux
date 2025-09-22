/*	$OpenBSD: intr.h,v 1.16 2025/04/26 11:10:28 visa Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _MACHINE_INTR_H_
#define _MACHINE_INTR_H_

#define __USE_MI_SOFTINTR

#include <sys/queue.h>
#include <sys/softintr.h>

struct cpu_info;
struct trapframe;

#define IPL_NONE	0
#define IPL_SOFTCLOCK	2
#define IPL_SOFTNET	3
#define IPL_SOFTTTY	4
#define IPL_BIO		5
#define IPL_NET		6
#define IPL_TTY		7
#define IPL_VM		IPL_TTY
#define IPL_AUDIO	8
#define IPL_CLOCK	9
#define IPL_STATCLOCK	IPL_CLOCK
#define IPL_SCHED	IPL_CLOCK
#define IPL_HIGH	IPL_CLOCK
#define IPL_IPI		10
#define NIPL		11

#define	IPL_MPFLOOR	IPL_TTY
/* Interrupt priority 'flags'. */
#define	IPL_IRQMASK	0xf	/* priority only */
#define	IPL_FLAGMASK	0xf00	/* flags only*/
#define	IPL_MPSAFE	0x100	/* 'mpsafe' interrupt, no kernel lock */

int	splraise(int);
int	spllower(int);
void	splx(int);

void	softintr(int);

#define spl0()		spllower(IPL_NONE)
#define splsoftclock()	splraise(IPL_SOFTCLOCK)
#define splsoftnet()	splraise(IPL_SOFTNET)
#define splsofttty()	splraise(IPL_SOFTTTY)
#define splbio()	splraise(IPL_BIO)
#define splnet()	splraise(IPL_NET)
#define spltty()	splraise(IPL_TTY)
#define splvm()		splraise(IPL_VM)
#define splclock()	splraise(IPL_CLOCK)
#define splstatclock()	splraise(IPL_STATCLOCK)
#define splsched()	splraise(IPL_SCHED)
#define splhigh()	splraise(IPL_HIGH)

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
#define	splsoftassert(wantipl)	splassert(wantipl)
#else
#define	splassert(wantipl)	do { /* nothing */ } while (0)
#define	splsoftassert(wantipl)	do { /* nothing */ } while (0)
#endif

void	intr_init(void);

#define intr_barrier(x)

#define IST_EDGE	0
#define IST_LEVEL	1

void	*intr_establish(uint32_t, int, int, struct cpu_info *,
	    int (*)(void *), void *, const char *);

#define IPI_NOP		0
#define IPI_DDB		(1 << 0)
#define IPI_SETPERF	(1 << 1)

void	intr_send_ipi(struct cpu_info *, int);

extern void (*_exi)(struct trapframe *);
extern void (*_hvi)(struct trapframe *);
extern void *(*_intr_establish)(uint32_t, int, int, struct cpu_info *,
	    int (*)(void *), void *, const char *);
extern void (*_intr_send_ipi)(void *);
extern void (*_setipl)(int);

struct interrupt_controller {
	int	ic_node;
	void	*ic_cookie;
	void	*(*ic_establish)(void *, int *, int, struct cpu_info *,
		    int (*)(void *), void *, char *);
	void	(*ic_send_ipi)(void *);

	LIST_ENTRY(interrupt_controller) ic_list;
	uint32_t ic_phandle;
	uint32_t ic_cells;
};

void	interrupt_controller_register(struct interrupt_controller *);

void	*fdt_intr_establish_idx_cpu(int, int, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	*fdt_intr_establish_imap(int, int *, int, int, int (*)(void *),
	    void *, char *);
void	*fdt_intr_establish_imap_cpu(int, int *, int, int,
	    struct cpu_info *, int (*)(void *), void *, char *);
void	fdt_intr_disestablish(void *);

#endif /* _MACHINE_INTR_H_ */
