/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * $FreeBSD$
 */

#ifndef _SYS_INTERRUPT_H_
#define _SYS_INTERRUPT_H_

#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/ck.h>

struct intr_event;
struct intr_thread;
struct trapframe;

/*
 * Describe a hardware interrupt handler.
 *
 * Multiple interrupt handlers for a specific event can be chained
 * together.
 */
struct intr_handler {
	driver_filter_t	*ih_filter;	/* Filter handler function. */
	driver_intr_t	*ih_handler;	/* Threaded handler function. */
	void		*ih_argument;	/* Argument to pass to handlers. */
	int		 ih_flags;
	char		 ih_name[MAXCOMLEN + 1]; /* Name of handler. */
	struct intr_event *ih_event;	/* Event we are connected to. */
	int		 ih_need;	/* Needs service. */
	CK_SLIST_ENTRY(intr_handler) ih_next; /* Next handler for this event. */
	u_char		 ih_pri;	/* Priority of this handler. */
	struct intr_thread *ih_thread;	/* Ithread for filtered handler. */
};

/* Interrupt handle flags kept in ih_flags */
#define	IH_EXCLUSIVE	0x00000002	/* Exclusive interrupt. */
#define	IH_ENTROPY	0x00000004	/* Device is a good entropy source. */
#define	IH_DEAD		0x00000008	/* Handler should be removed. */
#define	IH_SUSP		0x00000010	/* Device is powered down. */
#define	IH_CHANGED	0x40000000	/* Handler state is changed. */
#define	IH_MPSAFE	0x80000000	/* Handler does not need Giant. */

/*
 * Describe an interrupt event.  An event holds a list of handlers.
 * The 'pre_ithread', 'post_ithread', 'post_filter', and 'assign_cpu'
 * hooks are used to invoke MD code for certain operations.
 *
 * The 'pre_ithread' hook is called when an interrupt thread for
 * handlers without filters is scheduled.  It is responsible for
 * ensuring that 1) the system won't be swamped with an interrupt
 * storm from the associated source while the ithread runs and 2) the
 * current CPU is able to receive interrupts from other interrupt
 * sources.  The first is usually accomplished by disabling
 * level-triggered interrupts until the ithread completes.  The second
 * is accomplished on some platforms by acknowledging the interrupt
 * via an EOI.
 *
 * The 'post_ithread' hook is invoked when an ithread finishes.  It is
 * responsible for ensuring that the associated interrupt source will
 * trigger an interrupt when it is asserted in the future.  Usually
 * this is implemented by enabling a level-triggered interrupt that
 * was previously disabled via the 'pre_ithread' hook.
 *
 * The 'post_filter' hook is invoked when a filter handles an
 * interrupt.  It is responsible for ensuring that the current CPU is
 * able to receive interrupts again.  On some platforms this is done
 * by acknowledging the interrupts via an EOI.
 *
 * The 'assign_cpu' hook is used to bind an interrupt source to a
 * specific CPU.  If the interrupt cannot be bound, this function may
 * return an error.
 *
 * Note that device drivers may also use interrupt events to manage
 * multiplexing interrupt interrupt handler into handlers for child
 * devices.  In that case, the above hooks are not used.  The device
 * can create an event for its interrupt resource and register child
 * event handlers with that event.  It can then use
 * intr_event_execute_handlers() to execute non-filter handlers.
 * Currently filter handlers are not supported by this, but that can
 * be added by splitting out the filter loop from intr_event_handle()
 * if desired.
 */
struct intr_event {
	TAILQ_ENTRY(intr_event) ie_list;
	CK_SLIST_HEAD(, intr_handler) ie_handlers; /* Interrupt handlers. */
	char		ie_name[MAXCOMLEN + 1]; /* Individual event name. */
	char		ie_fullname[MAXCOMLEN + 1];
	struct mtx	ie_lock;
	void		*ie_source;	/* Cookie used by MD code. */
	struct intr_thread *ie_thread;	/* Thread we are connected to. */
	void		(*ie_pre_ithread)(void *);
	void		(*ie_post_ithread)(void *);
	void		(*ie_post_filter)(void *);
	int		(*ie_assign_cpu)(void *, int);
	int		ie_flags;
	int		ie_count;	/* Loop counter. */
	int		ie_warncnt;	/* Rate-check interrupt storm warns. */
	struct timeval	ie_warntm;
	int		ie_irq;		/* Physical irq number if !SOFT. */
	int		ie_cpu;		/* CPU this event is bound to. */
	volatile int	ie_phase;	/* Switched to establish a barrier. */
	volatile int	ie_active[2];	/* Filters in ISR context. */
};

/* Interrupt event flags kept in ie_flags. */
#define	IE_SOFT		0x000001	/* Software interrupt. */
#define	IE_ENTROPY	0x000002	/* Interrupt is an entropy source. */
#define	IE_ADDING_THREAD 0x000004	/* Currently building an ithread. */

/* Flags to pass to sched_swi. */
#define	SWI_DELAY	0x2

/*
 * Software interrupt numbers in priority order.  The priority determines
 * the priority of the corresponding interrupt thread.
 */
#define	SWI_TTY		0
#define	SWI_NET		1
#define	SWI_CAMBIO	2
#define	SWI_VM		3
#define	SWI_CLOCK	4
#define	SWI_TQ_FAST	5
#define	SWI_TQ		6
#define	SWI_TQ_GIANT	6

struct proc;

extern struct	intr_event *tty_intr_event;
extern struct	intr_event *clk_intr_event;
extern void	*vm_ih;

/* Counts and names for statistics (defined in MD code). */
#if defined(__amd64__) || defined(__i386__) || defined(__powerpc__)
extern u_long 	*intrcnt;	/* counts for for each device and stray */
extern char 	*intrnames;	/* string table containing device names */
#else
extern u_long 	intrcnt[];	/* counts for for each device and stray */
extern char 	intrnames[];	/* string table containing device names */
#endif
extern size_t	sintrcnt;	/* size of intrcnt table */
extern size_t	sintrnames;	/* size of intrnames table */

#ifdef DDB
void	db_dump_intr_event(struct intr_event *ie, int handlers);
#endif
u_char	intr_priority(enum intr_type flags);
int	intr_event_add_handler(struct intr_event *ie, const char *name,
	    driver_filter_t filter, driver_intr_t handler, void *arg, 
	    u_char pri, enum intr_type flags, void **cookiep);	    
int	intr_event_bind(struct intr_event *ie, int cpu);
int	intr_event_bind_irqonly(struct intr_event *ie, int cpu);
int	intr_event_bind_ithread(struct intr_event *ie, int cpu);
int	intr_event_create(struct intr_event **event, void *source,
	    int flags, int irq, void (*pre_ithread)(void *),
	    void (*post_ithread)(void *), void (*post_filter)(void *),
	    int (*assign_cpu)(void *, int), const char *fmt, ...)
	    __printflike(9, 10);
int	intr_event_describe_handler(struct intr_event *ie, void *cookie,
	    const char *descr);
int	intr_event_destroy(struct intr_event *ie);
int	intr_event_handle(struct intr_event *ie, struct trapframe *frame);
int	intr_event_remove_handler(void *cookie);
int	intr_event_suspend_handler(void *cookie);
int	intr_event_resume_handler(void *cookie);
int	intr_getaffinity(int irq, int mode, void *mask);
void	*intr_handler_source(void *cookie);
int	intr_setaffinity(int irq, int mode, void *mask);
void	_intr_drain(int irq);  /* Linux compat only. */
int	swi_add(struct intr_event **eventp, const char *name,
	    driver_intr_t handler, void *arg, int pri, enum intr_type flags,
	    void **cookiep);
void	swi_sched(void *cookie, int flags);
int	swi_remove(void *cookie);

#endif
