/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Oleksandr Tymoshenko
 * Copyright (c) 2002-2004 Juli Mallett <jmallett@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_hwpmc_hooks.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>

#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/cpuinfo.h>
#include <machine/cpuregs.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/trap.h>

static struct intr_event *hardintr_events[NHARD_IRQS];
static struct intr_event *softintr_events[NSOFT_IRQS];
static mips_intrcnt_t mips_intr_counters[NSOFT_IRQS + NHARD_IRQS];

static int intrcnt_index;

static cpu_intr_mask_t		hardintr_mask_func;
static cpu_intr_unmask_t	hardintr_unmask_func;

mips_intrcnt_t
mips_intrcnt_create(const char* name)
{
	mips_intrcnt_t counter = &intrcnt[intrcnt_index++];

	mips_intrcnt_setname(counter, name);
	return counter;
}

void
mips_intrcnt_setname(mips_intrcnt_t counter, const char *name)
{
	int idx = counter - intrcnt;

	KASSERT(counter != NULL, ("mips_intrcnt_setname: NULL counter"));

	snprintf(intrnames + (MAXCOMLEN + 1) * idx,
	    MAXCOMLEN + 1, "%-*s", MAXCOMLEN, name);
}

static void
mips_mask_hard_irq(void *source)
{
	uintptr_t irq = (uintptr_t)source;

	mips_wr_status(mips_rd_status() & ~(((1 << irq) << 8) << 2));
}

static void
mips_unmask_hard_irq(void *source)
{
	uintptr_t irq = (uintptr_t)source;

	mips_wr_status(mips_rd_status() | (((1 << irq) << 8) << 2));
}

static void
mips_mask_soft_irq(void *source)
{
	uintptr_t irq = (uintptr_t)source;

	mips_wr_status(mips_rd_status() & ~((1 << irq) << 8));
}

static void
mips_unmask_soft_irq(void *source)
{
	uintptr_t irq = (uintptr_t)source;

	mips_wr_status(mips_rd_status() | ((1 << irq) << 8));
}

/*
 * Perform initialization of interrupts prior to setting 
 * handlings
 */
void
cpu_init_interrupts()
{
	int i;
	char name[MAXCOMLEN + 1];

	/*
	 * Initialize all available vectors so spare IRQ
	 * would show up in systat output 
	 */
	for (i = 0; i < NSOFT_IRQS; i++) {
		snprintf(name, MAXCOMLEN + 1, "sint%d:", i);
		mips_intr_counters[i] = mips_intrcnt_create(name);
	}

	for (i = 0; i < NHARD_IRQS; i++) {
		snprintf(name, MAXCOMLEN + 1, "int%d:", i);
		mips_intr_counters[NSOFT_IRQS + i] = mips_intrcnt_create(name);
	}
}

void
cpu_set_hardintr_mask_func(cpu_intr_mask_t func)
{

	hardintr_mask_func = func;
}

void
cpu_set_hardintr_unmask_func(cpu_intr_unmask_t func)
{

	hardintr_unmask_func = func;
}

void
cpu_establish_hardintr(const char *name, driver_filter_t *filt,
    void (*handler)(void*), void *arg, int irq, int flags, void **cookiep)
{
	struct intr_event *event;
	int error;

	/*
	 * We have 6 levels, but thats 0 - 5 (not including 6)
	 */
	if (irq < 0 || irq >= NHARD_IRQS)
		panic("%s called for unknown hard intr %d", __func__, irq);

	if (hardintr_mask_func == NULL)
		hardintr_mask_func = mips_mask_hard_irq;

	if (hardintr_unmask_func == NULL)
		hardintr_unmask_func = mips_unmask_hard_irq;

	event = hardintr_events[irq];
	if (event == NULL) {
		error = intr_event_create(&event, (void *)(uintptr_t)irq, 0,
		    irq, hardintr_mask_func, hardintr_unmask_func,
		    NULL, NULL, "int%d", irq);
		if (error)
			return;
		hardintr_events[irq] = event;
		mips_unmask_hard_irq((void*)(uintptr_t)irq);
	}

	intr_event_add_handler(event, name, filt, handler, arg,
	    intr_priority(flags), flags, cookiep);

	mips_intrcnt_setname(mips_intr_counters[NSOFT_IRQS + irq],
			     event->ie_fullname);
}

void
cpu_establish_softintr(const char *name, driver_filter_t *filt,
    void (*handler)(void*), void *arg, int irq, int flags,
    void **cookiep)
{
	struct intr_event *event;
	int error;

#if 0
	printf("Establish SOFT IRQ %d: filt %p handler %p arg %p\n",
	    irq, filt, handler, arg);
#endif
	if (irq < 0 || irq > NSOFT_IRQS)
		panic("%s called for unknown hard intr %d", __func__, irq);

	event = softintr_events[irq];
	if (event == NULL) {
		error = intr_event_create(&event, (void *)(uintptr_t)irq, 0,
		    irq, mips_mask_soft_irq, mips_unmask_soft_irq,
		    NULL, NULL, "sint%d:", irq);
		if (error)
			return;
		softintr_events[irq] = event;
		mips_unmask_soft_irq((void*)(uintptr_t)irq);
	}

	intr_event_add_handler(event, name, filt, handler, arg,
	    intr_priority(flags), flags, cookiep);

	mips_intrcnt_setname(mips_intr_counters[irq], event->ie_fullname);
}

void
cpu_intr(struct trapframe *tf)
{
	struct intr_event *event;
	register_t cause, status;
	int hard, i, intr;

	critical_enter();

	cause = mips_rd_cause();
	status = mips_rd_status();
	intr = (cause & MIPS_INT_MASK) >> 8;
	/*
	 * Do not handle masked interrupts. They were masked by 
	 * pre_ithread function (mips_mask_XXX_intr) and will be 
	 * unmasked once ithread is through with handler
	 */
	intr &= (status & MIPS_INT_MASK) >> 8;
	while ((i = fls(intr)) != 0) {
		intr &= ~(1 << (i - 1));
		switch (i) {
		case 1: case 2:
			/* Software interrupt. */
			i--; /* Get a 0-offset interrupt. */
			hard = 0;
			event = softintr_events[i];
			mips_intrcnt_inc(mips_intr_counters[i]);
			break;
		default:
			/* Hardware interrupt. */
			i -= 2; /* Trim software interrupt bits. */
			i--; /* Get a 0-offset interrupt. */
			hard = 1;
			event = hardintr_events[i];
			mips_intrcnt_inc(mips_intr_counters[NSOFT_IRQS + i]);
			break;
		}

		if (!event || CK_SLIST_EMPTY(&event->ie_handlers)) {
			printf("stray %s interrupt %d\n",
			    hard ? "hard" : "soft", i);
			continue;
		}

		if (intr_event_handle(event, tf) != 0) {
			printf("stray %s interrupt %d\n", 
			    hard ? "hard" : "soft", i);
		}
	}

	KASSERT(i == 0, ("all interrupts handled"));

	critical_exit();

#ifdef HWPMC_HOOKS
	if (pmc_hook && (PCPU_GET(curthread)->td_pflags & TDP_CALLCHAIN))
		pmc_hook(PCPU_GET(curthread), PMC_FN_USER_CALLCHAIN, tf);
#endif
}
