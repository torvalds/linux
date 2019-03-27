/*	$NetBSD: intr.c,v 1.12 2003/07/15 00:24:41 lukem Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2004 Olivier Houchard.
 * Copyright (c) 1994-1998 Mark Brinicombe.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Mark Brinicombe
 *	for the NetBSD Project.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Soft interrupt and other generic interrupt functions.
 */

#include "opt_platform.h"
#include "opt_hwpmc_hooks.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/conf.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>
#include <sys/vmmeter.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/cpu.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <machine/fdt.h>
#endif

#define	INTRNAME_LEN	(MAXCOMLEN + 1)

typedef void (*mask_fn)(void *);

static struct intr_event *intr_events[NIRQ];

void	intr_irq_handler(struct trapframe *);

void (*arm_post_filter)(void *) = NULL;
int (*arm_config_irq)(int irq, enum intr_trigger trig,
    enum intr_polarity pol) = NULL;

/* Data for statistics reporting. */
u_long intrcnt[NIRQ];
char intrnames[(NIRQ * INTRNAME_LEN) + 1];
size_t sintrcnt = sizeof(intrcnt);
size_t sintrnames = sizeof(intrnames);

/*
 * Pre-format intrnames into an array of fixed-size strings containing spaces.
 * This allows us to avoid the need for an intermediate table of indices into
 * the names and counts arrays, while still meeting the requirements and
 * assumptions of vmstat(8) and the kdb "show intrcnt" command, the two
 * consumers of this data.
 */
static void
intr_init(void *unused)
{
	int i;

	for (i = 0; i < NIRQ; ++i) {
		snprintf(&intrnames[i * INTRNAME_LEN], INTRNAME_LEN, "%-*s",
		    INTRNAME_LEN - 1, "");
	}
}

SYSINIT(intr_init, SI_SUB_INTR, SI_ORDER_FIRST, intr_init, NULL);

#ifdef FDT
int
intr_fdt_map_irq(phandle_t iparent, pcell_t *intr, int icells)
{
	fdt_pic_decode_t intr_decode;
	phandle_t intr_parent;
	int i, rv, interrupt, trig, pol;

	intr_parent = OF_node_from_xref(iparent);
	for (i = 0; i < icells; i++)
		intr[i] = cpu_to_fdt32(intr[i]);

	for (i = 0; fdt_pic_table[i] != NULL; i++) {
		intr_decode = fdt_pic_table[i];
		rv = intr_decode(intr_parent, intr, &interrupt, &trig, &pol);

		if (rv == 0) {
			/* This was recognized as our PIC and decoded. */
			interrupt = FDT_MAP_IRQ(intr_parent, interrupt);
			return (interrupt);
		}
	}

	/* Not in table, so guess */
	interrupt = FDT_MAP_IRQ(intr_parent, fdt32_to_cpu(intr[0]));

	return (interrupt);
}
#endif

void
arm_setup_irqhandler(const char *name, driver_filter_t *filt,
    void (*hand)(void*), void *arg, int irq, int flags, void **cookiep)
{
	struct intr_event *event;
	int error;

	if (irq < 0 || irq >= NIRQ)
		return;
	event = intr_events[irq];
	if (event == NULL) {
		error = intr_event_create(&event, (void *)irq, 0, irq,
		    (mask_fn)arm_mask_irq, (mask_fn)arm_unmask_irq,
		    arm_post_filter, NULL, "intr%d:", irq);
		if (error)
			return;
		intr_events[irq] = event;
		snprintf(&intrnames[irq * INTRNAME_LEN], INTRNAME_LEN,
		    "irq%d: %-*s", irq, INTRNAME_LEN - 1, name);
	}
	intr_event_add_handler(event, name, filt, hand, arg,
	    intr_priority(flags), flags, cookiep);
}

int
arm_remove_irqhandler(int irq, void *cookie)
{
	struct intr_event *event;
	int error;

	event = intr_events[irq];
	arm_mask_irq(irq);

	error = intr_event_remove_handler(cookie);

	if (!CK_SLIST_EMPTY(&event->ie_handlers))
		arm_unmask_irq(irq);
	return (error);
}

void dosoftints(void);
void
dosoftints(void)
{
}

void
intr_irq_handler(struct trapframe *frame)
{
	struct intr_event *event;
	int i;

	VM_CNT_INC(v_intr);
	i = -1;
	while ((i = arm_get_next_irq(i)) != -1) {
		intrcnt[i]++;
		event = intr_events[i];
		if (intr_event_handle(event, frame) != 0) {
			/* XXX: Log stray IRQs */
			arm_mask_irq(i);
		}
	}
#ifdef HWPMC_HOOKS
	if (pmc_hook && (PCPU_GET(curthread)->td_pflags & TDP_CALLCHAIN))
		pmc_hook(PCPU_GET(curthread), PMC_FN_USER_CALLCHAIN, frame);
#endif
}
