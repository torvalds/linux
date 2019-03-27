/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 John Baldwin <jhb@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Machine dependent interrupt code for x86.  For x86, we have to
 * deal with different PICs.  Thus, we use the passed in vector to lookup
 * an interrupt source associated with that vector.  The interrupt source
 * describes which PIC the source belongs to and includes methods to handle
 * that source.
 */

#include "opt_atpic.h"
#include "opt_ddb.h"
#include "opt_smp.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/ktr.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/smp.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/vmmeter.h>
#include <machine/clock.h>
#include <machine/intr_machdep.h>
#include <machine/smp.h>
#ifdef DDB
#include <ddb/ddb.h>
#endif

#ifndef DEV_ATPIC
#include <machine/segments.h>
#include <machine/frame.h>
#include <dev/ic/i8259.h>
#include <x86/isa/icu.h>
#include <isa/isareg.h>
#endif

#include <vm/vm.h>

#define	MAX_STRAY_LOG	5

typedef void (*mask_fn)(void *);

static int intrcnt_index;
static struct intsrc **interrupt_sources;
#ifdef SMP
static struct intsrc **interrupt_sorted;
static int intrbalance;
SYSCTL_INT(_hw, OID_AUTO, intrbalance, CTLFLAG_RW, &intrbalance, 0,
    "Interrupt auto-balance interval (seconds).  Zero disables.");
static struct timeout_task intrbalance_task;
#endif
static struct sx intrsrc_lock;
static struct mtx intrpic_lock;
static struct mtx intrcnt_lock;
static TAILQ_HEAD(pics_head, pic) pics;
u_int num_io_irqs;

#if defined(SMP) && !defined(EARLY_AP_STARTUP)
static int assign_cpu;
#endif

u_long *intrcnt;
char *intrnames;
size_t sintrcnt = sizeof(intrcnt);
size_t sintrnames = sizeof(intrnames);
int nintrcnt;

static MALLOC_DEFINE(M_INTR, "intr", "Interrupt Sources");

static int	intr_assign_cpu(void *arg, int cpu);
static void	intr_disable_src(void *arg);
static void	intr_init(void *__dummy);
static int	intr_pic_registered(struct pic *pic);
static void	intrcnt_setname(const char *name, int index);
static void	intrcnt_updatename(struct intsrc *is);
static void	intrcnt_register(struct intsrc *is);

/*
 * SYSINIT levels for SI_SUB_INTR:
 *
 * SI_ORDER_FIRST: Initialize locks and pics TAILQ, xen_hvm_cpu_init
 * SI_ORDER_SECOND: Xen PICs
 * SI_ORDER_THIRD: Add I/O APIC PICs, alloc MSI and Xen IRQ ranges
 * SI_ORDER_FOURTH: Add 8259A PICs
 * SI_ORDER_FOURTH + 1: Finalize interrupt count and add interrupt sources
 * SI_ORDER_MIDDLE: SMP interrupt counters
 * SI_ORDER_ANY: Enable interrupts on BSP
 */

static int
intr_pic_registered(struct pic *pic)
{
	struct pic *p;

	TAILQ_FOREACH(p, &pics, pics) {
		if (p == pic)
			return (1);
	}
	return (0);
}

/*
 * Register a new interrupt controller (PIC).  This is to support suspend
 * and resume where we suspend/resume controllers rather than individual
 * sources.  This also allows controllers with no active sources (such as
 * 8259As in a system using the APICs) to participate in suspend and resume.
 */
int
intr_register_pic(struct pic *pic)
{
	int error;

	mtx_lock(&intrpic_lock);
	if (intr_pic_registered(pic))
		error = EBUSY;
	else {
		TAILQ_INSERT_TAIL(&pics, pic, pics);
		error = 0;
	}
	mtx_unlock(&intrpic_lock);
	return (error);
}

/*
 * Allocate interrupt source arrays and register interrupt sources
 * once the number of interrupts is known.
 */
static void
intr_init_sources(void *arg)
{
	struct pic *pic;

	MPASS(num_io_irqs > 0);

	interrupt_sources = mallocarray(num_io_irqs, sizeof(*interrupt_sources),
	    M_INTR, M_WAITOK | M_ZERO);
#ifdef SMP
	interrupt_sorted = mallocarray(num_io_irqs, sizeof(*interrupt_sorted),
	    M_INTR, M_WAITOK | M_ZERO);
#endif

	/*
	 * - 1 ??? dummy counter.
	 * - 2 counters for each I/O interrupt.
	 * - 1 counter for each CPU for lapic timer.
	 * - 1 counter for each CPU for the Hyper-V vmbus driver.
	 * - 8 counters for each CPU for IPI counters for SMP.
	 */
	nintrcnt = 1 + num_io_irqs * 2 + mp_ncpus * 2;
#ifdef COUNT_IPIS
	if (mp_ncpus > 1)
		nintrcnt += 8 * mp_ncpus;
#endif
	intrcnt = mallocarray(nintrcnt, sizeof(u_long), M_INTR, M_WAITOK |
	    M_ZERO);
	intrnames = mallocarray(nintrcnt, MAXCOMLEN + 1, M_INTR, M_WAITOK |
	    M_ZERO);
	sintrcnt = nintrcnt * sizeof(u_long);
	sintrnames = nintrcnt * (MAXCOMLEN + 1);

	intrcnt_setname("???", 0);
	intrcnt_index = 1;

	/*
	 * NB: intrpic_lock is not held here to avoid LORs due to
	 * malloc() in intr_register_source().  However, we are still
	 * single-threaded at this point in startup so the list of
	 * PICs shouldn't change.
	 */
	TAILQ_FOREACH(pic, &pics, pics) {
		if (pic->pic_register_sources != NULL)
			pic->pic_register_sources(pic);
	}
}
SYSINIT(intr_init_sources, SI_SUB_INTR, SI_ORDER_FOURTH + 1, intr_init_sources,
    NULL);

/*
 * Register a new interrupt source with the global interrupt system.
 * The global interrupts need to be disabled when this function is
 * called.
 */
int
intr_register_source(struct intsrc *isrc)
{
	int error, vector;

	KASSERT(intr_pic_registered(isrc->is_pic), ("unregistered PIC"));
	vector = isrc->is_pic->pic_vector(isrc);
	KASSERT(vector < num_io_irqs, ("IRQ %d too large (%u irqs)", vector,
	    num_io_irqs));
	if (interrupt_sources[vector] != NULL)
		return (EEXIST);
	error = intr_event_create(&isrc->is_event, isrc, 0, vector,
	    intr_disable_src, (mask_fn)isrc->is_pic->pic_enable_source,
	    (mask_fn)isrc->is_pic->pic_eoi_source, intr_assign_cpu, "irq%d:",
	    vector);
	if (error)
		return (error);
	sx_xlock(&intrsrc_lock);
	if (interrupt_sources[vector] != NULL) {
		sx_xunlock(&intrsrc_lock);
		intr_event_destroy(isrc->is_event);
		return (EEXIST);
	}
	intrcnt_register(isrc);
	interrupt_sources[vector] = isrc;
	isrc->is_handlers = 0;
	sx_xunlock(&intrsrc_lock);
	return (0);
}

struct intsrc *
intr_lookup_source(int vector)
{

	if (vector < 0 || vector >= num_io_irqs)
		return (NULL);
	return (interrupt_sources[vector]);
}

int
intr_add_handler(const char *name, int vector, driver_filter_t filter,
    driver_intr_t handler, void *arg, enum intr_type flags, void **cookiep,
    int domain)
{
	struct intsrc *isrc;
	int error;

	isrc = intr_lookup_source(vector);
	if (isrc == NULL)
		return (EINVAL);
	error = intr_event_add_handler(isrc->is_event, name, filter, handler,
	    arg, intr_priority(flags), flags, cookiep);
	if (error == 0) {
		sx_xlock(&intrsrc_lock);
		intrcnt_updatename(isrc);
		isrc->is_handlers++;
		if (isrc->is_handlers == 1) {
			isrc->is_domain = domain;
			isrc->is_pic->pic_enable_intr(isrc);
			isrc->is_pic->pic_enable_source(isrc);
		}
		sx_xunlock(&intrsrc_lock);
	}
	return (error);
}

int
intr_remove_handler(void *cookie)
{
	struct intsrc *isrc;
	int error;

	isrc = intr_handler_source(cookie);
	error = intr_event_remove_handler(cookie);
	if (error == 0) {
		sx_xlock(&intrsrc_lock);
		isrc->is_handlers--;
		if (isrc->is_handlers == 0) {
			isrc->is_pic->pic_disable_source(isrc, PIC_NO_EOI);
			isrc->is_pic->pic_disable_intr(isrc);
		}
		intrcnt_updatename(isrc);
		sx_xunlock(&intrsrc_lock);
	}
	return (error);
}

int
intr_config_intr(int vector, enum intr_trigger trig, enum intr_polarity pol)
{
	struct intsrc *isrc;

	isrc = intr_lookup_source(vector);
	if (isrc == NULL)
		return (EINVAL);
	return (isrc->is_pic->pic_config_intr(isrc, trig, pol));
}

static void
intr_disable_src(void *arg)
{
	struct intsrc *isrc;

	isrc = arg;
	isrc->is_pic->pic_disable_source(isrc, PIC_EOI);
}

void
intr_execute_handlers(struct intsrc *isrc, struct trapframe *frame)
{
	struct intr_event *ie;
	int vector;

	/*
	 * We count software interrupts when we process them.  The
	 * code here follows previous practice, but there's an
	 * argument for counting hardware interrupts when they're
	 * processed too.
	 */
	(*isrc->is_count)++;
	VM_CNT_INC(v_intr);

	ie = isrc->is_event;

	/*
	 * XXX: We assume that IRQ 0 is only used for the ISA timer
	 * device (clk).
	 */
	vector = isrc->is_pic->pic_vector(isrc);
	if (vector == 0)
		clkintr_pending = 1;

	/*
	 * For stray interrupts, mask and EOI the source, bump the
	 * stray count, and log the condition.
	 */
	if (intr_event_handle(ie, frame) != 0) {
		isrc->is_pic->pic_disable_source(isrc, PIC_EOI);
		(*isrc->is_straycount)++;
		if (*isrc->is_straycount < MAX_STRAY_LOG)
			log(LOG_ERR, "stray irq%d\n", vector);
		else if (*isrc->is_straycount == MAX_STRAY_LOG)
			log(LOG_CRIT,
			    "too many stray irq %d's: not logging anymore\n",
			    vector);
	}
}

void
intr_resume(bool suspend_cancelled)
{
	struct pic *pic;

#ifndef DEV_ATPIC
	atpic_reset();
#endif
	mtx_lock(&intrpic_lock);
	TAILQ_FOREACH(pic, &pics, pics) {
		if (pic->pic_resume != NULL)
			pic->pic_resume(pic, suspend_cancelled);
	}
	mtx_unlock(&intrpic_lock);
}

void
intr_suspend(void)
{
	struct pic *pic;

	mtx_lock(&intrpic_lock);
	TAILQ_FOREACH_REVERSE(pic, &pics, pics_head, pics) {
		if (pic->pic_suspend != NULL)
			pic->pic_suspend(pic);
	}
	mtx_unlock(&intrpic_lock);
}

static int
intr_assign_cpu(void *arg, int cpu)
{
#ifdef SMP
	struct intsrc *isrc;
	int error;

#ifdef EARLY_AP_STARTUP
	MPASS(mp_ncpus == 1 || smp_started);

	/* Nothing to do if there is only a single CPU. */
	if (mp_ncpus > 1 && cpu != NOCPU) {
#else
	/*
	 * Don't do anything during early boot.  We will pick up the
	 * assignment once the APs are started.
	 */
	if (assign_cpu && cpu != NOCPU) {
#endif
		isrc = arg;
		sx_xlock(&intrsrc_lock);
		error = isrc->is_pic->pic_assign_cpu(isrc, cpu_apic_ids[cpu]);
		if (error == 0)
			isrc->is_cpu = cpu;
		sx_xunlock(&intrsrc_lock);
	} else
		error = 0;
	return (error);
#else
	return (EOPNOTSUPP);
#endif
}

static void
intrcnt_setname(const char *name, int index)
{

	snprintf(intrnames + (MAXCOMLEN + 1) * index, MAXCOMLEN + 1, "%-*s",
	    MAXCOMLEN, name);
}

static void
intrcnt_updatename(struct intsrc *is)
{

	intrcnt_setname(is->is_event->ie_fullname, is->is_index);
}

static void
intrcnt_register(struct intsrc *is)
{
	char straystr[MAXCOMLEN + 1];

	KASSERT(is->is_event != NULL, ("%s: isrc with no event", __func__));
	mtx_lock_spin(&intrcnt_lock);
	MPASS(intrcnt_index + 2 <= nintrcnt);
	is->is_index = intrcnt_index;
	intrcnt_index += 2;
	snprintf(straystr, MAXCOMLEN + 1, "stray irq%d",
	    is->is_pic->pic_vector(is));
	intrcnt_updatename(is);
	is->is_count = &intrcnt[is->is_index];
	intrcnt_setname(straystr, is->is_index + 1);
	is->is_straycount = &intrcnt[is->is_index + 1];
	mtx_unlock_spin(&intrcnt_lock);
}

void
intrcnt_add(const char *name, u_long **countp)
{

	mtx_lock_spin(&intrcnt_lock);
	MPASS(intrcnt_index < nintrcnt);
	*countp = &intrcnt[intrcnt_index];
	intrcnt_setname(name, intrcnt_index);
	intrcnt_index++;
	mtx_unlock_spin(&intrcnt_lock);
}

static void
intr_init(void *dummy __unused)
{

	TAILQ_INIT(&pics);
	mtx_init(&intrpic_lock, "intrpic", NULL, MTX_DEF);
	sx_init(&intrsrc_lock, "intrsrc");
	mtx_init(&intrcnt_lock, "intrcnt", NULL, MTX_SPIN);
}
SYSINIT(intr_init, SI_SUB_INTR, SI_ORDER_FIRST, intr_init, NULL);

static void
intr_init_final(void *dummy __unused)
{

	/*
	 * Enable interrupts on the BSP after all of the interrupt
	 * controllers are initialized.  Device interrupts are still
	 * disabled in the interrupt controllers until interrupt
	 * handlers are registered.  Interrupts are enabled on each AP
	 * after their first context switch.
	 */
	enable_intr();
}
SYSINIT(intr_init_final, SI_SUB_INTR, SI_ORDER_ANY, intr_init_final, NULL);

#ifndef DEV_ATPIC
/* Initialize the two 8259A's to a known-good shutdown state. */
void
atpic_reset(void)
{

	outb(IO_ICU1, ICW1_RESET | ICW1_IC4);
	outb(IO_ICU1 + ICU_IMR_OFFSET, IDT_IO_INTS);
	outb(IO_ICU1 + ICU_IMR_OFFSET, IRQ_MASK(ICU_SLAVEID));
	outb(IO_ICU1 + ICU_IMR_OFFSET, MASTER_MODE);
	outb(IO_ICU1 + ICU_IMR_OFFSET, 0xff);
	outb(IO_ICU1, OCW3_SEL | OCW3_RR);

	outb(IO_ICU2, ICW1_RESET | ICW1_IC4);
	outb(IO_ICU2 + ICU_IMR_OFFSET, IDT_IO_INTS + 8);
	outb(IO_ICU2 + ICU_IMR_OFFSET, ICU_SLAVEID);
	outb(IO_ICU2 + ICU_IMR_OFFSET, SLAVE_MODE);
	outb(IO_ICU2 + ICU_IMR_OFFSET, 0xff);
	outb(IO_ICU2, OCW3_SEL | OCW3_RR);
}
#endif

/* Add a description to an active interrupt handler. */
int
intr_describe(u_int vector, void *ih, const char *descr)
{
	struct intsrc *isrc;
	int error;

	isrc = intr_lookup_source(vector);
	if (isrc == NULL)
		return (EINVAL);
	error = intr_event_describe_handler(isrc->is_event, ih, descr);
	if (error)
		return (error);
	intrcnt_updatename(isrc);
	return (0);
}

void
intr_reprogram(void)
{
	struct intsrc *is;
	u_int v;

	sx_xlock(&intrsrc_lock);
	for (v = 0; v < num_io_irqs; v++) {
		is = interrupt_sources[v];
		if (is == NULL)
			continue;
		if (is->is_pic->pic_reprogram_pin != NULL)
			is->is_pic->pic_reprogram_pin(is);
	}
	sx_xunlock(&intrsrc_lock);
}

#ifdef DDB
/*
 * Dump data about interrupt handlers
 */
DB_SHOW_COMMAND(irqs, db_show_irqs)
{
	struct intsrc **isrc;
	u_int i;
	int verbose;

	if (strcmp(modif, "v") == 0)
		verbose = 1;
	else
		verbose = 0;
	isrc = interrupt_sources;
	for (i = 0; i < num_io_irqs && !db_pager_quit; i++, isrc++)
		if (*isrc != NULL)
			db_dump_intr_event((*isrc)->is_event, verbose);
}
#endif

#ifdef SMP
/*
 * Support for balancing interrupt sources across CPUs.  For now we just
 * allocate CPUs round-robin.
 */

cpuset_t intr_cpus = CPUSET_T_INITIALIZER(0x1);
static int current_cpu[MAXMEMDOM];

static void
intr_init_cpus(void)
{
	int i;

	for (i = 0; i < vm_ndomains; i++) {
		current_cpu[i] = 0;
		if (!CPU_ISSET(current_cpu[i], &intr_cpus) ||
		    !CPU_ISSET(current_cpu[i], &cpuset_domain[i]))
			intr_next_cpu(i);
	}
}

/*
 * Return the CPU that the next interrupt source should use.  For now
 * this just returns the next local APIC according to round-robin.
 */
u_int
intr_next_cpu(int domain)
{
	u_int apic_id;

#ifdef EARLY_AP_STARTUP
	MPASS(mp_ncpus == 1 || smp_started);
	if (mp_ncpus == 1)
		return (PCPU_GET(apic_id));
#else
	/* Leave all interrupts on the BSP during boot. */
	if (!assign_cpu)
		return (PCPU_GET(apic_id));
#endif

	mtx_lock_spin(&icu_lock);
	apic_id = cpu_apic_ids[current_cpu[domain]];
	do {
		current_cpu[domain]++;
		if (current_cpu[domain] > mp_maxid)
			current_cpu[domain] = 0;
	} while (!CPU_ISSET(current_cpu[domain], &intr_cpus) ||
	    !CPU_ISSET(current_cpu[domain], &cpuset_domain[domain]));
	mtx_unlock_spin(&icu_lock);
	return (apic_id);
}

/* Attempt to bind the specified IRQ to the specified CPU. */
int
intr_bind(u_int vector, u_char cpu)
{
	struct intsrc *isrc;

	isrc = intr_lookup_source(vector);
	if (isrc == NULL)
		return (EINVAL);
	return (intr_event_bind(isrc->is_event, cpu));
}

/*
 * Add a CPU to our mask of valid CPUs that can be destinations of
 * interrupts.
 */
void
intr_add_cpu(u_int cpu)
{

	if (cpu >= MAXCPU)
		panic("%s: Invalid CPU ID", __func__);
	if (bootverbose)
		printf("INTR: Adding local APIC %d as a target\n",
		    cpu_apic_ids[cpu]);

	CPU_SET(cpu, &intr_cpus);
}

#ifdef EARLY_AP_STARTUP
static void
intr_smp_startup(void *arg __unused)
{

	intr_init_cpus();
	return;
}
SYSINIT(intr_smp_startup, SI_SUB_SMP, SI_ORDER_SECOND, intr_smp_startup,
    NULL);

#else
/*
 * Distribute all the interrupt sources among the available CPUs once the
 * AP's have been launched.
 */
static void
intr_shuffle_irqs(void *arg __unused)
{
	struct intsrc *isrc;
	u_int cpu, i;

	intr_init_cpus();
	/* Don't bother on UP. */
	if (mp_ncpus == 1)
		return;

	/* Round-robin assign a CPU to each enabled source. */
	sx_xlock(&intrsrc_lock);
	assign_cpu = 1;
	for (i = 0; i < num_io_irqs; i++) {
		isrc = interrupt_sources[i];
		if (isrc != NULL && isrc->is_handlers > 0) {
			/*
			 * If this event is already bound to a CPU,
			 * then assign the source to that CPU instead
			 * of picking one via round-robin.  Note that
			 * this is careful to only advance the
			 * round-robin if the CPU assignment succeeds.
			 */
			cpu = isrc->is_event->ie_cpu;
			if (cpu == NOCPU)
				cpu = current_cpu[isrc->is_domain];
			if (isrc->is_pic->pic_assign_cpu(isrc,
			    cpu_apic_ids[cpu]) == 0) {
				isrc->is_cpu = cpu;
				if (isrc->is_event->ie_cpu == NOCPU)
					intr_next_cpu(isrc->is_domain);
			}
		}
	}
	sx_xunlock(&intrsrc_lock);
}
SYSINIT(intr_shuffle_irqs, SI_SUB_SMP, SI_ORDER_SECOND, intr_shuffle_irqs,
    NULL);
#endif

/*
 * TODO: Export this information in a non-MD fashion, integrate with vmstat -i.
 */
static int
sysctl_hw_intrs(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sbuf;
	struct intsrc *isrc;
	u_int i;
	int error;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);

	sbuf_new_for_sysctl(&sbuf, NULL, 128, req);
	sx_slock(&intrsrc_lock);
	for (i = 0; i < num_io_irqs; i++) {
		isrc = interrupt_sources[i];
		if (isrc == NULL)
			continue;
		sbuf_printf(&sbuf, "%s:%d @cpu%d(domain%d): %ld\n",
		    isrc->is_event->ie_fullname,
		    isrc->is_index,
		    isrc->is_cpu,
		    isrc->is_domain,
		    *isrc->is_count);
	}

	sx_sunlock(&intrsrc_lock);
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}
SYSCTL_PROC(_hw, OID_AUTO, intrs, CTLTYPE_STRING | CTLFLAG_RW,
    0, 0, sysctl_hw_intrs, "A", "interrupt:number @cpu: count");

/*
 * Compare two, possibly NULL, entries in the interrupt source array
 * by load.
 */
static int
intrcmp(const void *one, const void *two)
{
	const struct intsrc *i1, *i2;

	i1 = *(const struct intsrc * const *)one;
	i2 = *(const struct intsrc * const *)two;
	if (i1 != NULL && i2 != NULL)
		return (*i1->is_count - *i2->is_count);
	if (i1 != NULL)
		return (1);
	if (i2 != NULL)
		return (-1);
	return (0);
}

/*
 * Balance IRQs across available CPUs according to load.
 */
static void
intr_balance(void *dummy __unused, int pending __unused)
{
	struct intsrc *isrc;
	int interval;
	u_int cpu;
	int i;

	interval = intrbalance;
	if (interval == 0)
		goto out;

	/*
	 * Sort interrupts according to count.
	 */
	sx_xlock(&intrsrc_lock);
	memcpy(interrupt_sorted, interrupt_sources, num_io_irqs *
	    sizeof(interrupt_sorted[0]));
	qsort(interrupt_sorted, num_io_irqs, sizeof(interrupt_sorted[0]),
	    intrcmp);

	/*
	 * Restart the scan from the same location to avoid moving in the
	 * common case.
	 */
	intr_init_cpus();

	/*
	 * Assign round-robin from most loaded to least.
	 */
	for (i = num_io_irqs - 1; i >= 0; i--) {
		isrc = interrupt_sorted[i];
		if (isrc == NULL  || isrc->is_event->ie_cpu != NOCPU)
			continue;
		cpu = current_cpu[isrc->is_domain];
		intr_next_cpu(isrc->is_domain);
		if (isrc->is_cpu != cpu &&
		    isrc->is_pic->pic_assign_cpu(isrc,
		    cpu_apic_ids[cpu]) == 0)
			isrc->is_cpu = cpu;
	}
	sx_xunlock(&intrsrc_lock);
out:
	taskqueue_enqueue_timeout(taskqueue_thread, &intrbalance_task,
	    interval ? hz * interval : hz * 60);

}

static void
intr_balance_init(void *dummy __unused)
{

	TIMEOUT_TASK_INIT(taskqueue_thread, &intrbalance_task, 0, intr_balance,
	    NULL);
	taskqueue_enqueue_timeout(taskqueue_thread, &intrbalance_task, hz);
}
SYSINIT(intr_balance_init, SI_SUB_SMP, SI_ORDER_ANY, intr_balance_init, NULL);

#else
/*
 * Always route interrupts to the current processor in the UP case.
 */
u_int
intr_next_cpu(int domain)
{

	return (PCPU_GET(apic_id));
}
#endif
