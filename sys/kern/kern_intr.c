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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_kstack_usage_prof.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/cpuset.h>
#include <sys/rtprio.h>
#include <sys/systm.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/random.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/unistd.h>
#include <sys/vmmeter.h>
#include <machine/atomic.h>
#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/stdarg.h>
#ifdef DDB
#include <ddb/ddb.h>
#include <ddb/db_sym.h>
#endif

/*
 * Describe an interrupt thread.  There is one of these per interrupt event.
 */
struct intr_thread {
	struct intr_event *it_event;
	struct thread *it_thread;	/* Kernel thread. */
	int	it_flags;		/* (j) IT_* flags. */
	int	it_need;		/* Needs service. */
};

/* Interrupt thread flags kept in it_flags */
#define	IT_DEAD		0x000001	/* Thread is waiting to exit. */
#define	IT_WAIT		0x000002	/* Thread is waiting for completion. */

struct	intr_entropy {
	struct	thread *td;
	uintptr_t event;
};

struct	intr_event *clk_intr_event;
struct	intr_event *tty_intr_event;
void	*vm_ih;
struct proc *intrproc;

static MALLOC_DEFINE(M_ITHREAD, "ithread", "Interrupt Threads");

static int intr_storm_threshold = 1000;
SYSCTL_INT(_hw, OID_AUTO, intr_storm_threshold, CTLFLAG_RWTUN,
    &intr_storm_threshold, 0,
    "Number of consecutive interrupts before storm protection is enabled");
static TAILQ_HEAD(, intr_event) event_list =
    TAILQ_HEAD_INITIALIZER(event_list);
static struct mtx event_lock;
MTX_SYSINIT(intr_event_list, &event_lock, "intr event list", MTX_DEF);

static void	intr_event_update(struct intr_event *ie);
static int	intr_event_schedule_thread(struct intr_event *ie);
static struct intr_thread *ithread_create(const char *name);
static void	ithread_destroy(struct intr_thread *ithread);
static void	ithread_execute_handlers(struct proc *p, 
		    struct intr_event *ie);
static void	ithread_loop(void *);
static void	ithread_update(struct intr_thread *ithd);
static void	start_softintr(void *);

/* Map an interrupt type to an ithread priority. */
u_char
intr_priority(enum intr_type flags)
{
	u_char pri;

	flags &= (INTR_TYPE_TTY | INTR_TYPE_BIO | INTR_TYPE_NET |
	    INTR_TYPE_CAM | INTR_TYPE_MISC | INTR_TYPE_CLK | INTR_TYPE_AV);
	switch (flags) {
	case INTR_TYPE_TTY:
		pri = PI_TTY;
		break;
	case INTR_TYPE_BIO:
		pri = PI_DISK;
		break;
	case INTR_TYPE_NET:
		pri = PI_NET;
		break;
	case INTR_TYPE_CAM:
		pri = PI_DISK;
		break;
	case INTR_TYPE_AV:
		pri = PI_AV;
		break;
	case INTR_TYPE_CLK:
		pri = PI_REALTIME;
		break;
	case INTR_TYPE_MISC:
		pri = PI_DULL;          /* don't care */
		break;
	default:
		/* We didn't specify an interrupt level. */
		panic("intr_priority: no interrupt type in flags");
	}

	return pri;
}

/*
 * Update an ithread based on the associated intr_event.
 */
static void
ithread_update(struct intr_thread *ithd)
{
	struct intr_event *ie;
	struct thread *td;
	u_char pri;

	ie = ithd->it_event;
	td = ithd->it_thread;
	mtx_assert(&ie->ie_lock, MA_OWNED);

	/* Determine the overall priority of this event. */
	if (CK_SLIST_EMPTY(&ie->ie_handlers))
		pri = PRI_MAX_ITHD;
	else
		pri = CK_SLIST_FIRST(&ie->ie_handlers)->ih_pri;

	/* Update name and priority. */
	strlcpy(td->td_name, ie->ie_fullname, sizeof(td->td_name));
#ifdef KTR
	sched_clear_tdname(td);
#endif
	thread_lock(td);
	sched_prio(td, pri);
	thread_unlock(td);
}

/*
 * Regenerate the full name of an interrupt event and update its priority.
 */
static void
intr_event_update(struct intr_event *ie)
{
	struct intr_handler *ih;
	char *last;
	int missed, space;

	/* Start off with no entropy and just the name of the event. */
	mtx_assert(&ie->ie_lock, MA_OWNED);
	strlcpy(ie->ie_fullname, ie->ie_name, sizeof(ie->ie_fullname));
	ie->ie_flags &= ~IE_ENTROPY;
	missed = 0;
	space = 1;

	/* Run through all the handlers updating values. */
	CK_SLIST_FOREACH(ih, &ie->ie_handlers, ih_next) {
		if (strlen(ie->ie_fullname) + strlen(ih->ih_name) + 1 <
		    sizeof(ie->ie_fullname)) {
			strcat(ie->ie_fullname, " ");
			strcat(ie->ie_fullname, ih->ih_name);
			space = 0;
		} else
			missed++;
		if (ih->ih_flags & IH_ENTROPY)
			ie->ie_flags |= IE_ENTROPY;
	}

	/*
	 * If there is only one handler and its name is too long, just copy in
	 * as much of the end of the name (includes the unit number) as will
	 * fit.  Otherwise, we have multiple handlers and not all of the names
	 * will fit.  Add +'s to indicate missing names.  If we run out of room
	 * and still have +'s to add, change the last character from a + to a *.
	 */
	if (missed == 1 && space == 1) {
		ih = CK_SLIST_FIRST(&ie->ie_handlers);
		missed = strlen(ie->ie_fullname) + strlen(ih->ih_name) + 2 -
		    sizeof(ie->ie_fullname);
		strcat(ie->ie_fullname, (missed == 0) ? " " : "-");
		strcat(ie->ie_fullname, &ih->ih_name[missed]);
		missed = 0;
	}
	last = &ie->ie_fullname[sizeof(ie->ie_fullname) - 2];
	while (missed-- > 0) {
		if (strlen(ie->ie_fullname) + 1 == sizeof(ie->ie_fullname)) {
			if (*last == '+') {
				*last = '*';
				break;
			} else
				*last = '+';
		} else if (space) {
			strcat(ie->ie_fullname, " +");
			space = 0;
		} else
			strcat(ie->ie_fullname, "+");
	}

	/*
	 * If this event has an ithread, update it's priority and
	 * name.
	 */
	if (ie->ie_thread != NULL)
		ithread_update(ie->ie_thread);
	CTR2(KTR_INTR, "%s: updated %s", __func__, ie->ie_fullname);
}

int
intr_event_create(struct intr_event **event, void *source, int flags, int irq,
    void (*pre_ithread)(void *), void (*post_ithread)(void *),
    void (*post_filter)(void *), int (*assign_cpu)(void *, int),
    const char *fmt, ...)
{
	struct intr_event *ie;
	va_list ap;

	/* The only valid flag during creation is IE_SOFT. */
	if ((flags & ~IE_SOFT) != 0)
		return (EINVAL);
	ie = malloc(sizeof(struct intr_event), M_ITHREAD, M_WAITOK | M_ZERO);
	ie->ie_source = source;
	ie->ie_pre_ithread = pre_ithread;
	ie->ie_post_ithread = post_ithread;
	ie->ie_post_filter = post_filter;
	ie->ie_assign_cpu = assign_cpu;
	ie->ie_flags = flags;
	ie->ie_irq = irq;
	ie->ie_cpu = NOCPU;
	CK_SLIST_INIT(&ie->ie_handlers);
	mtx_init(&ie->ie_lock, "intr event", NULL, MTX_DEF);

	va_start(ap, fmt);
	vsnprintf(ie->ie_name, sizeof(ie->ie_name), fmt, ap);
	va_end(ap);
	strlcpy(ie->ie_fullname, ie->ie_name, sizeof(ie->ie_fullname));
	mtx_lock(&event_lock);
	TAILQ_INSERT_TAIL(&event_list, ie, ie_list);
	mtx_unlock(&event_lock);
	if (event != NULL)
		*event = ie;
	CTR2(KTR_INTR, "%s: created %s", __func__, ie->ie_name);
	return (0);
}

/*
 * Bind an interrupt event to the specified CPU.  Note that not all
 * platforms support binding an interrupt to a CPU.  For those
 * platforms this request will fail.  Using a cpu id of NOCPU unbinds
 * the interrupt event.
 */
static int
_intr_event_bind(struct intr_event *ie, int cpu, bool bindirq, bool bindithread)
{
	lwpid_t id;
	int error;

	/* Need a CPU to bind to. */
	if (cpu != NOCPU && CPU_ABSENT(cpu))
		return (EINVAL);

	if (ie->ie_assign_cpu == NULL)
		return (EOPNOTSUPP);

	error = priv_check(curthread, PRIV_SCHED_CPUSET_INTR);
	if (error)
		return (error);

	/*
	 * If we have any ithreads try to set their mask first to verify
	 * permissions, etc.
	 */
	if (bindithread) {
		mtx_lock(&ie->ie_lock);
		if (ie->ie_thread != NULL) {
			id = ie->ie_thread->it_thread->td_tid;
			mtx_unlock(&ie->ie_lock);
			error = cpuset_setithread(id, cpu);
			if (error)
				return (error);
		} else
			mtx_unlock(&ie->ie_lock);
	}
	if (bindirq)
		error = ie->ie_assign_cpu(ie->ie_source, cpu);
	if (error) {
		if (bindithread) {
			mtx_lock(&ie->ie_lock);
			if (ie->ie_thread != NULL) {
				cpu = ie->ie_cpu;
				id = ie->ie_thread->it_thread->td_tid;
				mtx_unlock(&ie->ie_lock);
				(void)cpuset_setithread(id, cpu);
			} else
				mtx_unlock(&ie->ie_lock);
		}
		return (error);
	}

	if (bindirq) {
		mtx_lock(&ie->ie_lock);
		ie->ie_cpu = cpu;
		mtx_unlock(&ie->ie_lock);
	}

	return (error);
}

/*
 * Bind an interrupt event to the specified CPU.  For supported platforms, any
 * associated ithreads as well as the primary interrupt context will be bound
 * to the specificed CPU.
 */
int
intr_event_bind(struct intr_event *ie, int cpu)
{

	return (_intr_event_bind(ie, cpu, true, true));
}

/*
 * Bind an interrupt event to the specified CPU, but do not bind associated
 * ithreads.
 */
int
intr_event_bind_irqonly(struct intr_event *ie, int cpu)
{

	return (_intr_event_bind(ie, cpu, true, false));
}

/*
 * Bind an interrupt event's ithread to the specified CPU.
 */
int
intr_event_bind_ithread(struct intr_event *ie, int cpu)
{

	return (_intr_event_bind(ie, cpu, false, true));
}

static struct intr_event *
intr_lookup(int irq)
{
	struct intr_event *ie;

	mtx_lock(&event_lock);
	TAILQ_FOREACH(ie, &event_list, ie_list)
		if (ie->ie_irq == irq &&
		    (ie->ie_flags & IE_SOFT) == 0 &&
		    CK_SLIST_FIRST(&ie->ie_handlers) != NULL)
			break;
	mtx_unlock(&event_lock);
	return (ie);
}

int
intr_setaffinity(int irq, int mode, void *m)
{
	struct intr_event *ie;
	cpuset_t *mask;
	int cpu, n;

	mask = m;
	cpu = NOCPU;
	/*
	 * If we're setting all cpus we can unbind.  Otherwise make sure
	 * only one cpu is in the set.
	 */
	if (CPU_CMP(cpuset_root, mask)) {
		for (n = 0; n < CPU_SETSIZE; n++) {
			if (!CPU_ISSET(n, mask))
				continue;
			if (cpu != NOCPU)
				return (EINVAL);
			cpu = n;
		}
	}
	ie = intr_lookup(irq);
	if (ie == NULL)
		return (ESRCH);
	switch (mode) {
	case CPU_WHICH_IRQ:
		return (intr_event_bind(ie, cpu));
	case CPU_WHICH_INTRHANDLER:
		return (intr_event_bind_irqonly(ie, cpu));
	case CPU_WHICH_ITHREAD:
		return (intr_event_bind_ithread(ie, cpu));
	default:
		return (EINVAL);
	}
}

int
intr_getaffinity(int irq, int mode, void *m)
{
	struct intr_event *ie;
	struct thread *td;
	struct proc *p;
	cpuset_t *mask;
	lwpid_t id;
	int error;

	mask = m;
	ie = intr_lookup(irq);
	if (ie == NULL)
		return (ESRCH);

	error = 0;
	CPU_ZERO(mask);
	switch (mode) {
	case CPU_WHICH_IRQ:
	case CPU_WHICH_INTRHANDLER:
		mtx_lock(&ie->ie_lock);
		if (ie->ie_cpu == NOCPU)
			CPU_COPY(cpuset_root, mask);
		else
			CPU_SET(ie->ie_cpu, mask);
		mtx_unlock(&ie->ie_lock);
		break;
	case CPU_WHICH_ITHREAD:
		mtx_lock(&ie->ie_lock);
		if (ie->ie_thread == NULL) {
			mtx_unlock(&ie->ie_lock);
			CPU_COPY(cpuset_root, mask);
		} else {
			id = ie->ie_thread->it_thread->td_tid;
			mtx_unlock(&ie->ie_lock);
			error = cpuset_which(CPU_WHICH_TID, id, &p, &td, NULL);
			if (error != 0)
				return (error);
			CPU_COPY(&td->td_cpuset->cs_mask, mask);
			PROC_UNLOCK(p);
		}
	default:
		return (EINVAL);
	}
	return (0);
}

int
intr_event_destroy(struct intr_event *ie)
{

	mtx_lock(&event_lock);
	mtx_lock(&ie->ie_lock);
	if (!CK_SLIST_EMPTY(&ie->ie_handlers)) {
		mtx_unlock(&ie->ie_lock);
		mtx_unlock(&event_lock);
		return (EBUSY);
	}
	TAILQ_REMOVE(&event_list, ie, ie_list);
#ifndef notyet
	if (ie->ie_thread != NULL) {
		ithread_destroy(ie->ie_thread);
		ie->ie_thread = NULL;
	}
#endif
	mtx_unlock(&ie->ie_lock);
	mtx_unlock(&event_lock);
	mtx_destroy(&ie->ie_lock);
	free(ie, M_ITHREAD);
	return (0);
}

static struct intr_thread *
ithread_create(const char *name)
{
	struct intr_thread *ithd;
	struct thread *td;
	int error;

	ithd = malloc(sizeof(struct intr_thread), M_ITHREAD, M_WAITOK | M_ZERO);

	error = kproc_kthread_add(ithread_loop, ithd, &intrproc,
		    &td, RFSTOPPED | RFHIGHPID,
		    0, "intr", "%s", name);
	if (error)
		panic("kproc_create() failed with %d", error);
	thread_lock(td);
	sched_class(td, PRI_ITHD);
	TD_SET_IWAIT(td);
	thread_unlock(td);
	td->td_pflags |= TDP_ITHREAD;
	ithd->it_thread = td;
	CTR2(KTR_INTR, "%s: created %s", __func__, name);
	return (ithd);
}

static void
ithread_destroy(struct intr_thread *ithread)
{
	struct thread *td;

	CTR2(KTR_INTR, "%s: killing %s", __func__, ithread->it_event->ie_name);
	td = ithread->it_thread;
	thread_lock(td);
	ithread->it_flags |= IT_DEAD;
	if (TD_AWAITING_INTR(td)) {
		TD_CLR_IWAIT(td);
		sched_add(td, SRQ_INTR);
	}
	thread_unlock(td);
}

int
intr_event_add_handler(struct intr_event *ie, const char *name,
    driver_filter_t filter, driver_intr_t handler, void *arg, u_char pri,
    enum intr_type flags, void **cookiep)
{
	struct intr_handler *ih, *temp_ih;
	struct intr_handler **prevptr;
	struct intr_thread *it;

	if (ie == NULL || name == NULL || (handler == NULL && filter == NULL))
		return (EINVAL);

	/* Allocate and populate an interrupt handler structure. */
	ih = malloc(sizeof(struct intr_handler), M_ITHREAD, M_WAITOK | M_ZERO);
	ih->ih_filter = filter;
	ih->ih_handler = handler;
	ih->ih_argument = arg;
	strlcpy(ih->ih_name, name, sizeof(ih->ih_name));
	ih->ih_event = ie;
	ih->ih_pri = pri;
	if (flags & INTR_EXCL)
		ih->ih_flags = IH_EXCLUSIVE;
	if (flags & INTR_MPSAFE)
		ih->ih_flags |= IH_MPSAFE;
	if (flags & INTR_ENTROPY)
		ih->ih_flags |= IH_ENTROPY;

	/* We can only have one exclusive handler in a event. */
	mtx_lock(&ie->ie_lock);
	if (!CK_SLIST_EMPTY(&ie->ie_handlers)) {
		if ((flags & INTR_EXCL) ||
		    (CK_SLIST_FIRST(&ie->ie_handlers)->ih_flags & IH_EXCLUSIVE)) {
			mtx_unlock(&ie->ie_lock);
			free(ih, M_ITHREAD);
			return (EINVAL);
		}
	}

	/* Create a thread if we need one. */
	while (ie->ie_thread == NULL && handler != NULL) {
		if (ie->ie_flags & IE_ADDING_THREAD)
			msleep(ie, &ie->ie_lock, 0, "ithread", 0);
		else {
			ie->ie_flags |= IE_ADDING_THREAD;
			mtx_unlock(&ie->ie_lock);
			it = ithread_create("intr: newborn");
			mtx_lock(&ie->ie_lock);
			ie->ie_flags &= ~IE_ADDING_THREAD;
			ie->ie_thread = it;
			it->it_event = ie;
			ithread_update(it);
			wakeup(ie);
		}
	}

	/* Add the new handler to the event in priority order. */
	CK_SLIST_FOREACH_PREVPTR(temp_ih, prevptr, &ie->ie_handlers, ih_next) {
		if (temp_ih->ih_pri > ih->ih_pri)
			break;
	}
	CK_SLIST_INSERT_PREVPTR(prevptr, temp_ih, ih, ih_next);

	intr_event_update(ie);

	CTR3(KTR_INTR, "%s: added %s to %s", __func__, ih->ih_name,
	    ie->ie_name);
	mtx_unlock(&ie->ie_lock);

	if (cookiep != NULL)
		*cookiep = ih;
	return (0);
}

/*
 * Append a description preceded by a ':' to the name of the specified
 * interrupt handler.
 */
int
intr_event_describe_handler(struct intr_event *ie, void *cookie,
    const char *descr)
{
	struct intr_handler *ih;
	size_t space;
	char *start;

	mtx_lock(&ie->ie_lock);
#ifdef INVARIANTS
	CK_SLIST_FOREACH(ih, &ie->ie_handlers, ih_next) {
		if (ih == cookie)
			break;
	}
	if (ih == NULL) {
		mtx_unlock(&ie->ie_lock);
		panic("handler %p not found in interrupt event %p", cookie, ie);
	}
#endif
	ih = cookie;

	/*
	 * Look for an existing description by checking for an
	 * existing ":".  This assumes device names do not include
	 * colons.  If one is found, prepare to insert the new
	 * description at that point.  If one is not found, find the
	 * end of the name to use as the insertion point.
	 */
	start = strchr(ih->ih_name, ':');
	if (start == NULL)
		start = strchr(ih->ih_name, 0);

	/*
	 * See if there is enough remaining room in the string for the
	 * description + ":".  The "- 1" leaves room for the trailing
	 * '\0'.  The "+ 1" accounts for the colon.
	 */
	space = sizeof(ih->ih_name) - (start - ih->ih_name) - 1;
	if (strlen(descr) + 1 > space) {
		mtx_unlock(&ie->ie_lock);
		return (ENOSPC);
	}

	/* Append a colon followed by the description. */
	*start = ':';
	strcpy(start + 1, descr);
	intr_event_update(ie);
	mtx_unlock(&ie->ie_lock);
	return (0);
}

/*
 * Return the ie_source field from the intr_event an intr_handler is
 * associated with.
 */
void *
intr_handler_source(void *cookie)
{
	struct intr_handler *ih;
	struct intr_event *ie;

	ih = (struct intr_handler *)cookie;
	if (ih == NULL)
		return (NULL);
	ie = ih->ih_event;
	KASSERT(ie != NULL,
	    ("interrupt handler \"%s\" has a NULL interrupt event",
	    ih->ih_name));
	return (ie->ie_source);
}

/*
 * If intr_event_handle() is running in the ISR context at the time of the call,
 * then wait for it to complete.
 */
static void
intr_event_barrier(struct intr_event *ie)
{
	int phase;

	mtx_assert(&ie->ie_lock, MA_OWNED);
	phase = ie->ie_phase;

	/*
	 * Switch phase to direct future interrupts to the other active counter.
	 * Make sure that any preceding stores are visible before the switch.
	 */
	KASSERT(ie->ie_active[!phase] == 0, ("idle phase has activity"));
	atomic_store_rel_int(&ie->ie_phase, !phase);

	/*
	 * This code cooperates with wait-free iteration of ie_handlers
	 * in intr_event_handle.
	 * Make sure that the removal and the phase update are not reordered
	 * with the active count check.
	 * Note that no combination of acquire and release fences can provide
	 * that guarantee as Store->Load sequences can always be reordered.
	 */
	atomic_thread_fence_seq_cst();

	/*
	 * Now wait on the inactive phase.
	 * The acquire fence is needed so that that all post-barrier accesses
	 * are after the check.
	 */
	while (ie->ie_active[phase] > 0)
		cpu_spinwait();
	atomic_thread_fence_acq();
}

static void
intr_handler_barrier(struct intr_handler *handler)
{
	struct intr_event *ie;

	ie = handler->ih_event;
	mtx_assert(&ie->ie_lock, MA_OWNED);
	KASSERT((handler->ih_flags & IH_DEAD) == 0,
	    ("update for a removed handler"));

	if (ie->ie_thread == NULL) {
		intr_event_barrier(ie);
		return;
	}
	if ((handler->ih_flags & IH_CHANGED) == 0) {
		handler->ih_flags |= IH_CHANGED;
		intr_event_schedule_thread(ie);
	}
	while ((handler->ih_flags & IH_CHANGED) != 0)
		msleep(handler, &ie->ie_lock, 0, "ih_barr", 0);
}

/*
 * Sleep until an ithread finishes executing an interrupt handler.
 *
 * XXX Doesn't currently handle interrupt filters or fast interrupt
 * handlers.  This is intended for compatibility with linux drivers
 * only.  Do not use in BSD code.
 */
void
_intr_drain(int irq)
{
	struct intr_event *ie;
	struct intr_thread *ithd;
	struct thread *td;

	ie = intr_lookup(irq);
	if (ie == NULL)
		return;
	if (ie->ie_thread == NULL)
		return;
	ithd = ie->ie_thread;
	td = ithd->it_thread;
	/*
	 * We set the flag and wait for it to be cleared to avoid
	 * long delays with potentially busy interrupt handlers
	 * were we to only sample TD_AWAITING_INTR() every tick.
	 */
	thread_lock(td);
	if (!TD_AWAITING_INTR(td)) {
		ithd->it_flags |= IT_WAIT;
		while (ithd->it_flags & IT_WAIT) {
			thread_unlock(td);
			pause("idrain", 1);
			thread_lock(td);
		}
	}
	thread_unlock(td);
	return;
}

int
intr_event_remove_handler(void *cookie)
{
	struct intr_handler *handler = (struct intr_handler *)cookie;
	struct intr_event *ie;
	struct intr_handler *ih;
	struct intr_handler **prevptr;
#ifdef notyet
	int dead;
#endif

	if (handler == NULL)
		return (EINVAL);
	ie = handler->ih_event;
	KASSERT(ie != NULL,
	    ("interrupt handler \"%s\" has a NULL interrupt event",
	    handler->ih_name));

	mtx_lock(&ie->ie_lock);
	CTR3(KTR_INTR, "%s: removing %s from %s", __func__, handler->ih_name,
	    ie->ie_name);
	CK_SLIST_FOREACH_PREVPTR(ih, prevptr, &ie->ie_handlers, ih_next) {
		if (ih == handler)
			break;
	}
	if (ih == NULL) {
		panic("interrupt handler \"%s\" not found in "
		    "interrupt event \"%s\"", handler->ih_name, ie->ie_name);
	}

	/*
	 * If there is no ithread, then directly remove the handler.  Note that
	 * intr_event_handle() iterates ie_handlers in a lock-less fashion, so
	 * care needs to be taken to keep ie_handlers consistent and to free
	 * the removed handler only when ie_handlers is quiescent.
	 */
	if (ie->ie_thread == NULL) {
		CK_SLIST_REMOVE_PREVPTR(prevptr, ih, ih_next);
		intr_event_barrier(ie);
		intr_event_update(ie);
		mtx_unlock(&ie->ie_lock);
		free(handler, M_ITHREAD);
		return (0);
	}

	/*
	 * Let the interrupt thread do the job.
	 * The interrupt source is disabled when the interrupt thread is
	 * running, so it does not have to worry about interaction with
	 * intr_event_handle().
	 */
	KASSERT((handler->ih_flags & IH_DEAD) == 0,
	    ("duplicate handle remove"));
	handler->ih_flags |= IH_DEAD;
	intr_event_schedule_thread(ie);
	while (handler->ih_flags & IH_DEAD)
		msleep(handler, &ie->ie_lock, 0, "iev_rmh", 0);
	intr_event_update(ie);

#ifdef notyet
	/*
	 * XXX: This could be bad in the case of ppbus(8).  Also, I think
	 * this could lead to races of stale data when servicing an
	 * interrupt.
	 */
	dead = 1;
	CK_SLIST_FOREACH(ih, &ie->ie_handlers, ih_next) {
		if (ih->ih_handler != NULL) {
			dead = 0;
			break;
		}
	}
	if (dead) {
		ithread_destroy(ie->ie_thread);
		ie->ie_thread = NULL;
	}
#endif
	mtx_unlock(&ie->ie_lock);
	free(handler, M_ITHREAD);
	return (0);
}

int
intr_event_suspend_handler(void *cookie)
{
	struct intr_handler *handler = (struct intr_handler *)cookie;
	struct intr_event *ie;

	if (handler == NULL)
		return (EINVAL);
	ie = handler->ih_event;
	KASSERT(ie != NULL,
	    ("interrupt handler \"%s\" has a NULL interrupt event",
	    handler->ih_name));
	mtx_lock(&ie->ie_lock);
	handler->ih_flags |= IH_SUSP;
	intr_handler_barrier(handler);
	mtx_unlock(&ie->ie_lock);
	return (0);
}

int
intr_event_resume_handler(void *cookie)
{
	struct intr_handler *handler = (struct intr_handler *)cookie;
	struct intr_event *ie;

	if (handler == NULL)
		return (EINVAL);
	ie = handler->ih_event;
	KASSERT(ie != NULL,
	    ("interrupt handler \"%s\" has a NULL interrupt event",
	    handler->ih_name));

	/*
	 * intr_handler_barrier() acts not only as a barrier,
	 * it also allows to check for any pending interrupts.
	 */
	mtx_lock(&ie->ie_lock);
	handler->ih_flags &= ~IH_SUSP;
	intr_handler_barrier(handler);
	mtx_unlock(&ie->ie_lock);
	return (0);
}

static int
intr_event_schedule_thread(struct intr_event *ie)
{
	struct intr_entropy entropy;
	struct intr_thread *it;
	struct thread *td;
	struct thread *ctd;

	/*
	 * If no ithread or no handlers, then we have a stray interrupt.
	 */
	if (ie == NULL || CK_SLIST_EMPTY(&ie->ie_handlers) ||
	    ie->ie_thread == NULL)
		return (EINVAL);

	ctd = curthread;
	it = ie->ie_thread;
	td = it->it_thread;

	/*
	 * If any of the handlers for this ithread claim to be good
	 * sources of entropy, then gather some.
	 */
	if (ie->ie_flags & IE_ENTROPY) {
		entropy.event = (uintptr_t)ie;
		entropy.td = ctd;
		random_harvest_queue(&entropy, sizeof(entropy), RANDOM_INTERRUPT);
	}

	KASSERT(td->td_proc != NULL, ("ithread %s has no process", ie->ie_name));

	/*
	 * Set it_need to tell the thread to keep running if it is already
	 * running.  Then, lock the thread and see if we actually need to
	 * put it on the runqueue.
	 *
	 * Use store_rel to arrange that the store to ih_need in
	 * swi_sched() is before the store to it_need and prepare for
	 * transfer of this order to loads in the ithread.
	 */
	atomic_store_rel_int(&it->it_need, 1);
	thread_lock(td);
	if (TD_AWAITING_INTR(td)) {
		CTR3(KTR_INTR, "%s: schedule pid %d (%s)", __func__, td->td_proc->p_pid,
		    td->td_name);
		TD_CLR_IWAIT(td);
		sched_add(td, SRQ_INTR);
	} else {
		CTR5(KTR_INTR, "%s: pid %d (%s): it_need %d, state %d",
		    __func__, td->td_proc->p_pid, td->td_name, it->it_need, td->td_state);
	}
	thread_unlock(td);

	return (0);
}

/*
 * Allow interrupt event binding for software interrupt handlers -- a no-op,
 * since interrupts are generated in software rather than being directed by
 * a PIC.
 */
static int
swi_assign_cpu(void *arg, int cpu)
{

	return (0);
}

/*
 * Add a software interrupt handler to a specified event.  If a given event
 * is not specified, then a new event is created.
 */
int
swi_add(struct intr_event **eventp, const char *name, driver_intr_t handler,
	    void *arg, int pri, enum intr_type flags, void **cookiep)
{
	struct intr_event *ie;
	int error;

	if (flags & INTR_ENTROPY)
		return (EINVAL);

	ie = (eventp != NULL) ? *eventp : NULL;

	if (ie != NULL) {
		if (!(ie->ie_flags & IE_SOFT))
			return (EINVAL);
	} else {
		error = intr_event_create(&ie, NULL, IE_SOFT, 0,
		    NULL, NULL, NULL, swi_assign_cpu, "swi%d:", pri);
		if (error)
			return (error);
		if (eventp != NULL)
			*eventp = ie;
	}
	error = intr_event_add_handler(ie, name, NULL, handler, arg,
	    PI_SWI(pri), flags, cookiep);
	return (error);
}

/*
 * Schedule a software interrupt thread.
 */
void
swi_sched(void *cookie, int flags)
{
	struct intr_handler *ih = (struct intr_handler *)cookie;
	struct intr_event *ie = ih->ih_event;
	struct intr_entropy entropy;
	int error __unused;

	CTR3(KTR_INTR, "swi_sched: %s %s need=%d", ie->ie_name, ih->ih_name,
	    ih->ih_need);

	entropy.event = (uintptr_t)ih;
	entropy.td = curthread;
	random_harvest_queue(&entropy, sizeof(entropy), RANDOM_SWI);

	/*
	 * Set ih_need for this handler so that if the ithread is already
	 * running it will execute this handler on the next pass.  Otherwise,
	 * it will execute it the next time it runs.
	 */
	ih->ih_need = 1;

	if (!(flags & SWI_DELAY)) {
		VM_CNT_INC(v_soft);
		error = intr_event_schedule_thread(ie);
		KASSERT(error == 0, ("stray software interrupt"));
	}
}

/*
 * Remove a software interrupt handler.  Currently this code does not
 * remove the associated interrupt event if it becomes empty.  Calling code
 * may do so manually via intr_event_destroy(), but that's not really
 * an optimal interface.
 */
int
swi_remove(void *cookie)
{

	return (intr_event_remove_handler(cookie));
}

static void
intr_event_execute_handlers(struct proc *p, struct intr_event *ie)
{
	struct intr_handler *ih, *ihn, *ihp;

	ihp = NULL;
	CK_SLIST_FOREACH_SAFE(ih, &ie->ie_handlers, ih_next, ihn) {
		/*
		 * If this handler is marked for death, remove it from
		 * the list of handlers and wake up the sleeper.
		 */
		if (ih->ih_flags & IH_DEAD) {
			mtx_lock(&ie->ie_lock);
			if (ihp == NULL)
				CK_SLIST_REMOVE_HEAD(&ie->ie_handlers, ih_next);
			else
				CK_SLIST_REMOVE_AFTER(ihp, ih_next);
			ih->ih_flags &= ~IH_DEAD;
			wakeup(ih);
			mtx_unlock(&ie->ie_lock);
			continue;
		}

		/*
		 * Now that we know that the current element won't be removed
		 * update the previous element.
		 */
		ihp = ih;

		if ((ih->ih_flags & IH_CHANGED) != 0) {
			mtx_lock(&ie->ie_lock);
			ih->ih_flags &= ~IH_CHANGED;
			wakeup(ih);
			mtx_unlock(&ie->ie_lock);
		}

		/* Skip filter only handlers */
		if (ih->ih_handler == NULL)
			continue;

		/* Skip suspended handlers */
		if ((ih->ih_flags & IH_SUSP) != 0)
			continue;

		/*
		 * For software interrupt threads, we only execute
		 * handlers that have their need flag set.  Hardware
		 * interrupt threads always invoke all of their handlers.
		 *
		 * ih_need can only be 0 or 1.  Failed cmpset below
		 * means that there is no request to execute handlers,
		 * so a retry of the cmpset is not needed.
		 */
		if ((ie->ie_flags & IE_SOFT) != 0 &&
		    atomic_cmpset_int(&ih->ih_need, 1, 0) == 0)
			continue;

		/* Execute this handler. */
		CTR6(KTR_INTR, "%s: pid %d exec %p(%p) for %s flg=%x",
		    __func__, p->p_pid, (void *)ih->ih_handler, 
		    ih->ih_argument, ih->ih_name, ih->ih_flags);

		if (!(ih->ih_flags & IH_MPSAFE))
			mtx_lock(&Giant);
		ih->ih_handler(ih->ih_argument);
		if (!(ih->ih_flags & IH_MPSAFE))
			mtx_unlock(&Giant);
	}
}

static void
ithread_execute_handlers(struct proc *p, struct intr_event *ie)
{

	/* Interrupt handlers should not sleep. */
	if (!(ie->ie_flags & IE_SOFT))
		THREAD_NO_SLEEPING();
	intr_event_execute_handlers(p, ie);
	if (!(ie->ie_flags & IE_SOFT))
		THREAD_SLEEPING_OK();

	/*
	 * Interrupt storm handling:
	 *
	 * If this interrupt source is currently storming, then throttle
	 * it to only fire the handler once  per clock tick.
	 *
	 * If this interrupt source is not currently storming, but the
	 * number of back to back interrupts exceeds the storm threshold,
	 * then enter storming mode.
	 */
	if (intr_storm_threshold != 0 && ie->ie_count >= intr_storm_threshold &&
	    !(ie->ie_flags & IE_SOFT)) {
		/* Report the message only once every second. */
		if (ppsratecheck(&ie->ie_warntm, &ie->ie_warncnt, 1)) {
			printf(
	"interrupt storm detected on \"%s\"; throttling interrupt source\n",
			    ie->ie_name);
		}
		pause("istorm", 1);
	} else
		ie->ie_count++;

	/*
	 * Now that all the handlers have had a chance to run, reenable
	 * the interrupt source.
	 */
	if (ie->ie_post_ithread != NULL)
		ie->ie_post_ithread(ie->ie_source);
}

/*
 * This is the main code for interrupt threads.
 */
static void
ithread_loop(void *arg)
{
	struct intr_thread *ithd;
	struct intr_event *ie;
	struct thread *td;
	struct proc *p;
	int wake;

	td = curthread;
	p = td->td_proc;
	ithd = (struct intr_thread *)arg;
	KASSERT(ithd->it_thread == td,
	    ("%s: ithread and proc linkage out of sync", __func__));
	ie = ithd->it_event;
	ie->ie_count = 0;
	wake = 0;

	/*
	 * As long as we have interrupts outstanding, go through the
	 * list of handlers, giving each one a go at it.
	 */
	for (;;) {
		/*
		 * If we are an orphaned thread, then just die.
		 */
		if (ithd->it_flags & IT_DEAD) {
			CTR3(KTR_INTR, "%s: pid %d (%s) exiting", __func__,
			    p->p_pid, td->td_name);
			free(ithd, M_ITHREAD);
			kthread_exit();
		}

		/*
		 * Service interrupts.  If another interrupt arrives while
		 * we are running, it will set it_need to note that we
		 * should make another pass.
		 *
		 * The load_acq part of the following cmpset ensures
		 * that the load of ih_need in ithread_execute_handlers()
		 * is ordered after the load of it_need here.
		 */
		while (atomic_cmpset_acq_int(&ithd->it_need, 1, 0) != 0)
			ithread_execute_handlers(p, ie);
		WITNESS_WARN(WARN_PANIC, NULL, "suspending ithread");
		mtx_assert(&Giant, MA_NOTOWNED);

		/*
		 * Processed all our interrupts.  Now get the sched
		 * lock.  This may take a while and it_need may get
		 * set again, so we have to check it again.
		 */
		thread_lock(td);
		if (atomic_load_acq_int(&ithd->it_need) == 0 &&
		    (ithd->it_flags & (IT_DEAD | IT_WAIT)) == 0) {
			TD_SET_IWAIT(td);
			ie->ie_count = 0;
			mi_switch(SW_VOL | SWT_IWAIT, NULL);
		}
		if (ithd->it_flags & IT_WAIT) {
			wake = 1;
			ithd->it_flags &= ~IT_WAIT;
		}
		thread_unlock(td);
		if (wake) {
			wakeup(ithd);
			wake = 0;
		}
	}
}

/*
 * Main interrupt handling body.
 *
 * Input:
 * o ie:                        the event connected to this interrupt.
 * o frame:                     some archs (i.e. i386) pass a frame to some.
 *                              handlers as their main argument.
 * Return value:
 * o 0:                         everything ok.
 * o EINVAL:                    stray interrupt.
 */
int
intr_event_handle(struct intr_event *ie, struct trapframe *frame)
{
	struct intr_handler *ih;
	struct trapframe *oldframe;
	struct thread *td;
	int phase;
	int ret;
	bool filter, thread;

	td = curthread;

#ifdef KSTACK_USAGE_PROF
	intr_prof_stack_use(td, frame);
#endif

	/* An interrupt with no event or handlers is a stray interrupt. */
	if (ie == NULL || CK_SLIST_EMPTY(&ie->ie_handlers))
		return (EINVAL);

	/*
	 * Execute fast interrupt handlers directly.
	 * To support clock handlers, if a handler registers
	 * with a NULL argument, then we pass it a pointer to
	 * a trapframe as its argument.
	 */
	td->td_intr_nesting_level++;
	filter = false;
	thread = false;
	ret = 0;
	critical_enter();
	oldframe = td->td_intr_frame;
	td->td_intr_frame = frame;

	phase = ie->ie_phase;
	atomic_add_int(&ie->ie_active[phase], 1);

	/*
	 * This fence is required to ensure that no later loads are
	 * re-ordered before the ie_active store.
	 */
	atomic_thread_fence_seq_cst();

	CK_SLIST_FOREACH(ih, &ie->ie_handlers, ih_next) {
		if ((ih->ih_flags & IH_SUSP) != 0)
			continue;
		if (ih->ih_filter == NULL) {
			thread = true;
			continue;
		}
		CTR4(KTR_INTR, "%s: exec %p(%p) for %s", __func__,
		    ih->ih_filter, ih->ih_argument == NULL ? frame :
		    ih->ih_argument, ih->ih_name);
		if (ih->ih_argument == NULL)
			ret = ih->ih_filter(frame);
		else
			ret = ih->ih_filter(ih->ih_argument);
		KASSERT(ret == FILTER_STRAY ||
		    ((ret & (FILTER_SCHEDULE_THREAD | FILTER_HANDLED)) != 0 &&
		    (ret & ~(FILTER_SCHEDULE_THREAD | FILTER_HANDLED)) == 0),
		    ("%s: incorrect return value %#x from %s", __func__, ret,
		    ih->ih_name));
		filter = filter || ret == FILTER_HANDLED;

		/*
		 * Wrapper handler special handling:
		 *
		 * in some particular cases (like pccard and pccbb),
		 * the _real_ device handler is wrapped in a couple of
		 * functions - a filter wrapper and an ithread wrapper.
		 * In this case (and just in this case), the filter wrapper
		 * could ask the system to schedule the ithread and mask
		 * the interrupt source if the wrapped handler is composed
		 * of just an ithread handler.
		 *
		 * TODO: write a generic wrapper to avoid people rolling
		 * their own.
		 */
		if (!thread) {
			if (ret == FILTER_SCHEDULE_THREAD)
				thread = true;
		}
	}
	atomic_add_rel_int(&ie->ie_active[phase], -1);

	td->td_intr_frame = oldframe;

	if (thread) {
		if (ie->ie_pre_ithread != NULL)
			ie->ie_pre_ithread(ie->ie_source);
	} else {
		if (ie->ie_post_filter != NULL)
			ie->ie_post_filter(ie->ie_source);
	}

	/* Schedule the ithread if needed. */
	if (thread) {
		int error __unused;

		error =  intr_event_schedule_thread(ie);
		KASSERT(error == 0, ("bad stray interrupt"));
	}
	critical_exit();
	td->td_intr_nesting_level--;
#ifdef notyet
	/* The interrupt is not aknowledged by any filter and has no ithread. */
	if (!thread && !filter)
		return (EINVAL);
#endif
	return (0);
}

#ifdef DDB
/*
 * Dump details about an interrupt handler
 */
static void
db_dump_intrhand(struct intr_handler *ih)
{
	int comma;

	db_printf("\t%-10s ", ih->ih_name);
	switch (ih->ih_pri) {
	case PI_REALTIME:
		db_printf("CLK ");
		break;
	case PI_AV:
		db_printf("AV  ");
		break;
	case PI_TTY:
		db_printf("TTY ");
		break;
	case PI_NET:
		db_printf("NET ");
		break;
	case PI_DISK:
		db_printf("DISK");
		break;
	case PI_DULL:
		db_printf("DULL");
		break;
	default:
		if (ih->ih_pri >= PI_SOFT)
			db_printf("SWI ");
		else
			db_printf("%4u", ih->ih_pri);
		break;
	}
	db_printf(" ");
	if (ih->ih_filter != NULL) {
		db_printf("[F]");
		db_printsym((uintptr_t)ih->ih_filter, DB_STGY_PROC);
	}
	if (ih->ih_handler != NULL) {
		if (ih->ih_filter != NULL)
			db_printf(",");
		db_printf("[H]");
		db_printsym((uintptr_t)ih->ih_handler, DB_STGY_PROC);
	}
	db_printf("(%p)", ih->ih_argument);
	if (ih->ih_need ||
	    (ih->ih_flags & (IH_EXCLUSIVE | IH_ENTROPY | IH_DEAD |
	    IH_MPSAFE)) != 0) {
		db_printf(" {");
		comma = 0;
		if (ih->ih_flags & IH_EXCLUSIVE) {
			if (comma)
				db_printf(", ");
			db_printf("EXCL");
			comma = 1;
		}
		if (ih->ih_flags & IH_ENTROPY) {
			if (comma)
				db_printf(", ");
			db_printf("ENTROPY");
			comma = 1;
		}
		if (ih->ih_flags & IH_DEAD) {
			if (comma)
				db_printf(", ");
			db_printf("DEAD");
			comma = 1;
		}
		if (ih->ih_flags & IH_MPSAFE) {
			if (comma)
				db_printf(", ");
			db_printf("MPSAFE");
			comma = 1;
		}
		if (ih->ih_need) {
			if (comma)
				db_printf(", ");
			db_printf("NEED");
		}
		db_printf("}");
	}
	db_printf("\n");
}

/*
 * Dump details about a event.
 */
void
db_dump_intr_event(struct intr_event *ie, int handlers)
{
	struct intr_handler *ih;
	struct intr_thread *it;
	int comma;

	db_printf("%s ", ie->ie_fullname);
	it = ie->ie_thread;
	if (it != NULL)
		db_printf("(pid %d)", it->it_thread->td_proc->p_pid);
	else
		db_printf("(no thread)");
	if ((ie->ie_flags & (IE_SOFT | IE_ENTROPY | IE_ADDING_THREAD)) != 0 ||
	    (it != NULL && it->it_need)) {
		db_printf(" {");
		comma = 0;
		if (ie->ie_flags & IE_SOFT) {
			db_printf("SOFT");
			comma = 1;
		}
		if (ie->ie_flags & IE_ENTROPY) {
			if (comma)
				db_printf(", ");
			db_printf("ENTROPY");
			comma = 1;
		}
		if (ie->ie_flags & IE_ADDING_THREAD) {
			if (comma)
				db_printf(", ");
			db_printf("ADDING_THREAD");
			comma = 1;
		}
		if (it != NULL && it->it_need) {
			if (comma)
				db_printf(", ");
			db_printf("NEED");
		}
		db_printf("}");
	}
	db_printf("\n");

	if (handlers)
		CK_SLIST_FOREACH(ih, &ie->ie_handlers, ih_next)
		    db_dump_intrhand(ih);
}

/*
 * Dump data about interrupt handlers
 */
DB_SHOW_COMMAND(intr, db_show_intr)
{
	struct intr_event *ie;
	int all, verbose;

	verbose = strchr(modif, 'v') != NULL;
	all = strchr(modif, 'a') != NULL;
	TAILQ_FOREACH(ie, &event_list, ie_list) {
		if (!all && CK_SLIST_EMPTY(&ie->ie_handlers))
			continue;
		db_dump_intr_event(ie, verbose);
		if (db_pager_quit)
			break;
	}
}
#endif /* DDB */

/*
 * Start standard software interrupt threads
 */
static void
start_softintr(void *dummy)
{

	if (swi_add(NULL, "vm", swi_vm, NULL, SWI_VM, INTR_MPSAFE, &vm_ih))
		panic("died while creating vm swi ithread");
}
SYSINIT(start_softintr, SI_SUB_SOFTINTR, SI_ORDER_FIRST, start_softintr,
    NULL);

/*
 * Sysctls used by systat and others: hw.intrnames and hw.intrcnt.
 * The data for this machine dependent, and the declarations are in machine
 * dependent code.  The layout of intrnames and intrcnt however is machine
 * independent.
 *
 * We do not know the length of intrcnt and intrnames at compile time, so
 * calculate things at run time.
 */
static int
sysctl_intrnames(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_handle_opaque(oidp, intrnames, sintrnames, req));
}

SYSCTL_PROC(_hw, OID_AUTO, intrnames, CTLTYPE_OPAQUE | CTLFLAG_RD,
    NULL, 0, sysctl_intrnames, "", "Interrupt Names");

static int
sysctl_intrcnt(SYSCTL_HANDLER_ARGS)
{
#ifdef SCTL_MASK32
	uint32_t *intrcnt32;
	unsigned i;
	int error;

	if (req->flags & SCTL_MASK32) {
		if (!req->oldptr)
			return (sysctl_handle_opaque(oidp, NULL, sintrcnt / 2, req));
		intrcnt32 = malloc(sintrcnt / 2, M_TEMP, M_NOWAIT);
		if (intrcnt32 == NULL)
			return (ENOMEM);
		for (i = 0; i < sintrcnt / sizeof (u_long); i++)
			intrcnt32[i] = intrcnt[i];
		error = sysctl_handle_opaque(oidp, intrcnt32, sintrcnt / 2, req);
		free(intrcnt32, M_TEMP);
		return (error);
	}
#endif
	return (sysctl_handle_opaque(oidp, intrcnt, sintrcnt, req));
}

SYSCTL_PROC(_hw, OID_AUTO, intrcnt, CTLTYPE_OPAQUE | CTLFLAG_RD,
    NULL, 0, sysctl_intrcnt, "", "Interrupt Counts");

#ifdef DDB
/*
 * DDB command to dump the interrupt statistics.
 */
DB_SHOW_COMMAND(intrcnt, db_show_intrcnt)
{
	u_long *i;
	char *cp;
	u_int j;

	cp = intrnames;
	j = 0;
	for (i = intrcnt; j < (sintrcnt / sizeof(u_long)) && !db_pager_quit;
	    i++, j++) {
		if (*cp == '\0')
			break;
		if (*i != 0)
			db_printf("%s\t%lu\n", cp, *i);
		cp += strlen(cp) + 1;
	}
}
#endif
