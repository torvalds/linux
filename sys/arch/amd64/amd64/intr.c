/*	$OpenBSD: intr.c,v 1.63 2025/06/11 09:57:01 kettenis Exp $	*/
/*	$NetBSD: intr.c,v 1.3 2003/03/03 22:16:20 fvdl Exp $	*/

/*
 * Copyright 2002 (c) Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
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

/* #define	INTRDEBUG */

#include <sys/param.h> 
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/errno.h>

#include <machine/atomic.h>
#include <machine/i8259.h>
#include <machine/cpu.h>
#include <machine/pio.h>
#include <machine/cpufunc.h>

#include "lapic.h"
#include "xen.h"
#include "hyperv.h"

#if NLAPIC > 0
#include <machine/i82489var.h>
#endif

struct pic softintr_pic = {
        {0, {NULL}, NULL, 0, "softintr_pic0", NULL, 0, 0},
        PIC_SOFT,
#ifdef MULTIPROCESSOR
	{},
#endif
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

const int softintr_to_ssir[NSOFTINTR] = {
	SIR_CLOCK,
	SIR_NET,
	SIR_TTY,
};

int intr_suspended;
struct intrhand *intr_nowake;

/*
 * Fill in default interrupt table (in case of spurious interrupt
 * during configuration of kernel), setup interrupt control unit
 */
void
intr_default_setup(void)
{
	int i;

	/* icu vectors */
	for (i = 0; i < NUM_LEGACY_IRQS; i++) {
		idt_allocmap[ICU_OFFSET + i] = 1;
		setgate(&idt[ICU_OFFSET + i],
		    i8259_stubs[i].ist_entry, 0, SDT_SYS386IGT,
		    SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	}

	/*
	 * Eventually might want to check if it's actually there.
	 */
	i8259_default_setup();
}

/*
 * Handle a NMI, possibly a machine check.
 * return true to panic system, false to ignore.
 */
int
x86_nmi(void)
{
	log(LOG_CRIT, "NMI port 61 %x, port 70 %x\n", inb(0x61), inb(0x70));
	return(0);
}

/*
 * Recalculate the interrupt masks from scratch.
 */
void
intr_calculatemasks(struct cpu_info *ci)
{
	int irq, level;
	u_int64_t unusedirqs, intrlevel[MAX_INTR_SOURCES];
	struct intrhand *q;

	/* First, figure out which levels each IRQ uses. */
	unusedirqs = 0xffffffffffffffffUL;
	for (irq = 0; irq < MAX_INTR_SOURCES; irq++) {
		int levels = 0;

		if (ci->ci_isources[irq] == NULL) {
			intrlevel[irq] = 0;
			continue;
		}
		for (q = ci->ci_isources[irq]->is_handlers; q; q = q->ih_next)
			levels |= (1 << q->ih_level);
		intrlevel[irq] = levels;
		if (levels)
			unusedirqs &= ~(1UL << irq);
	}

	/* Then figure out which IRQs use each level. */
	for (level = 0; level < NIPL; level++) {
		u_int64_t irqs = 0;
		for (irq = 0; irq < MAX_INTR_SOURCES; irq++)
			if (intrlevel[irq] & (1 << level))
				irqs |= (1UL << irq);
		ci->ci_imask[level] = irqs | unusedirqs;
	}

	for (level = 0; level< (NIPL - 1); level++)
		ci->ci_imask[level + 1] |= ci->ci_imask[level];

	for (irq = 0; irq < MAX_INTR_SOURCES; irq++) {
		int maxlevel = IPL_NONE;
		int minlevel = IPL_HIGH;

		if (ci->ci_isources[irq] == NULL)
			continue;
		for (q = ci->ci_isources[irq]->is_handlers; q;
		     q = q->ih_next) {
			if (q->ih_level < minlevel)
				minlevel = q->ih_level;
			if (q->ih_level > maxlevel)
				maxlevel = q->ih_level;
		}
		ci->ci_isources[irq]->is_maxlevel = maxlevel;
		ci->ci_isources[irq]->is_minlevel = minlevel;
	}

	for (level = 0; level < NIPL; level++)
		ci->ci_iunmask[level] = ~ci->ci_imask[level];
}

int
intr_allocate_slot_cpu(struct cpu_info *ci, struct pic *pic, int pin,
    int *index)
{
	int start, slot, i;
	struct intrsource *isp;

	start = CPU_IS_PRIMARY(ci) ? NUM_LEGACY_IRQS : 0;
	slot = -1;

	for (i = 0; i < start; i++) {
		isp = ci->ci_isources[i];
		if (isp != NULL && isp->is_pic == pic && isp->is_pin == pin) {
			slot = i;
			start = MAX_INTR_SOURCES;
			break;
		}
	}
	for (i = start; i < MAX_INTR_SOURCES ; i++) {
		isp = ci->ci_isources[i];
		if (isp != NULL && isp->is_pic == pic && isp->is_pin == pin) {
			slot = i;
			break;
		}
		if (isp == NULL && slot == -1) {
			slot = i;
			continue;
		}
	}
	if (slot == -1) {
		return EBUSY;
	}

	isp = ci->ci_isources[slot];
	if (isp == NULL) {
		isp = malloc(sizeof (struct intrsource), M_DEVBUF,
		    M_NOWAIT|M_ZERO);
		if (isp == NULL) {
			return ENOMEM;
		}
		snprintf(isp->is_evname, sizeof (isp->is_evname),
		    "pin %d", pin);
		ci->ci_isources[slot] = isp;
	}

	*index = slot;
	return 0;
}

/*
 * A simple round-robin allocator to assign interrupts to CPUs.
 */
int
intr_allocate_slot(struct pic *pic, int legacy_irq, int pin, int level,
    struct cpu_info **cip, int *index, int *idt_slot)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	struct intrsource *isp;
	int slot, idtvec, error;

	/*
	 * If a legacy IRQ is wanted, try to use a fixed slot pointing
	 * at the primary CPU. In the case of IO APICs, multiple pins
	 * may map to one legacy IRQ, but they should not be shared
	 * in that case, so the first one gets the legacy slot, but
	 * a subsequent allocation with a different pin will get
	 * a different slot.
	 */
	if (legacy_irq != -1) {
		ci = &cpu_info_primary;
		/* must check for duplicate pic + pin first */
		for (slot = 0 ; slot < MAX_INTR_SOURCES ; slot++) {
			isp = ci->ci_isources[slot];
			if (isp != NULL && isp->is_pic == pic &&
			    isp->is_pin == pin ) {
				goto duplicate;
			}
		}
		slot = legacy_irq;
		isp = ci->ci_isources[slot];
		if (isp == NULL) {
			isp = malloc(sizeof (struct intrsource), M_DEVBUF,
			     M_NOWAIT|M_ZERO);
			if (isp == NULL)
				return ENOMEM;
			snprintf(isp->is_evname, sizeof (isp->is_evname),
			    "pin %d", pin);

			ci->ci_isources[slot] = isp;
		} else {
			if (isp->is_pic != pic || isp->is_pin != pin) {
				if (pic == &i8259_pic)
					return EINVAL;
				goto other;
			}
		}
duplicate:
		if (pic == &i8259_pic)
			idtvec = ICU_OFFSET + legacy_irq;
		else {
#ifdef IOAPIC_HWMASK
			if (level > isp->is_maxlevel) {
#else
			if (isp->is_minlevel == 0 || level < isp->is_minlevel) {
#endif
				idtvec = idt_vec_alloc(APIC_LEVEL(level),
				    IDT_INTR_HIGH);
				if (idtvec == 0)
					return EBUSY;
			} else
				idtvec = isp->is_idtvec;
		}
	} else {
other:
		/*
		 * Otherwise, look for a free slot elsewhere. If cip is null, it
		 * means try primary cpu but accept secondary, otherwise we need
		 * a slot on the requested cpu.
		 */
		if (*cip == NULL)
			ci = &cpu_info_primary;
		else
			ci = *cip;

		error = intr_allocate_slot_cpu(ci, pic, pin, &slot);
		if (error == 0)
			goto found;
		/* Can't alloc on the requested cpu, fail. */
		if (*cip != NULL)
			return EBUSY;

		/*
		 * ..now try the others.
		 */
		CPU_INFO_FOREACH(cii, ci) {
			if (CPU_IS_PRIMARY(ci))
				continue;
			error = intr_allocate_slot_cpu(ci, pic, pin, &slot);
			if (error == 0)
				goto found;
		}
		return EBUSY;
found:
		if (pic->pic_allocidtvec) {
			idtvec = pic->pic_allocidtvec(pic, pin,
			    APIC_LEVEL(level), IDT_INTR_HIGH);
		} else {
			idtvec = idt_vec_alloc(APIC_LEVEL(level),
			    IDT_INTR_HIGH);
		}
		if (idtvec == 0) {
			free(ci->ci_isources[slot], M_DEVBUF,
			    sizeof (struct intrsource));
			ci->ci_isources[slot] = NULL;
			return EBUSY;
		}
	}
	*idt_slot = idtvec;
	*index = slot;
	*cip = ci;
	return 0;
}

/*
 * True if the system has any non-level interrupts which are shared
 * on the same pin.
 */
int	intr_shared_edge;

void *
intr_establish(int legacy_irq, struct pic *pic, int pin, int type, int level,
    struct cpu_info *ci, int (*handler)(void *), void *arg, const char *what)
{
	struct intrhand **p, *q, *ih;
	int slot, error, idt_vec;
	struct intrsource *source;
	struct intrstub *stubp;
	int flags;

#ifdef DIAGNOSTIC
	if (legacy_irq != -1 && (legacy_irq < 0 || legacy_irq > 15))
		panic("intr_establish: bad legacy IRQ value");

	if (legacy_irq == -1 && pic == &i8259_pic)
		panic("intr_establish: non-legacy IRQ on i8259");
#endif

	flags = level & (IPL_MPSAFE | IPL_WAKEUP);
	level &= ~(IPL_MPSAFE | IPL_WAKEUP);

	KASSERT(level <= IPL_TTY || level >= IPL_CLOCK || flags & IPL_MPSAFE);

	error = intr_allocate_slot(pic, legacy_irq, pin, level, &ci, &slot,
	    &idt_vec);
	if (error != 0) {
		printf("failed to allocate interrupt slot for PIC %s pin %d\n",
		    pic->pic_dev.dv_xname, pin);
		return NULL;
	}

	/* no point in sleeping unless someone can free memory. */
	ih = malloc(sizeof *ih, M_DEVBUF, cold ? M_NOWAIT : M_WAITOK);
	if (ih == NULL) {
		printf("intr_establish: can't allocate handler info\n");
		return NULL;
	}

	source = ci->ci_isources[slot];

	if (source->is_handlers != NULL &&
	    source->is_pic->pic_type != pic->pic_type) {
		free(ih, M_DEVBUF, sizeof(*ih));
		printf("intr_establish: can't share intr source between "
		       "different PIC types (legacy_irq %d pin %d slot %d)\n",
		    legacy_irq, pin, slot);
		return NULL;
	}

	source->is_pin = pin;
	source->is_pic = pic;

	switch (source->is_type) {
	case IST_NONE:
		source->is_type = type;
		break;
	case IST_EDGE:
		intr_shared_edge = 1;
		/* FALLTHROUGH */
	case IST_LEVEL:
		if (source->is_type == type)
			break;
	case IST_PULSE:
		if (type != IST_NONE) {
			printf("intr_establish: pic %s pin %d: can't share "
			       "type %d with %d\n", pic->pic_name, pin,
				source->is_type, type);
			free(ih, M_DEVBUF, sizeof(*ih));
			return NULL;
		}
		break;
	default:
		panic("intr_establish: bad intr type %d for pic %s pin %d",
		    source->is_type, pic->pic_dev.dv_xname, pin);
	}

	if (!cold)
		pic->pic_hwmask(pic, pin);

	/*
	 * Figure out where to put the handler.
	 * This is O(N^2), but we want to preserve the order, and N is
	 * generally small.
	 */
	for (p = &ci->ci_isources[slot]->is_handlers;
	     (q = *p) != NULL && q->ih_level > level;
	     p = &q->ih_next)
		;

	ih->ih_fun = handler;
	ih->ih_arg = arg;
	ih->ih_next = *p;
	ih->ih_level = level;
	ih->ih_flags = flags; 
	ih->ih_pin = pin;
	ih->ih_cpu = ci;
	ih->ih_slot = slot;
	evcount_attach(&ih->ih_count, what, &source->is_idtvec);

	*p = ih;

	intr_calculatemasks(ci);

	if (ci->ci_isources[slot]->is_resume == NULL ||
	    source->is_idtvec != idt_vec) {
		if (source->is_idtvec != 0 && source->is_idtvec != idt_vec)
			idt_vec_free(source->is_idtvec);
		source->is_idtvec = idt_vec;
		stubp = type == IST_LEVEL ?
		    &pic->pic_level_stubs[slot] : &pic->pic_edge_stubs[slot];
		ci->ci_isources[slot]->is_resume = stubp->ist_resume;
		ci->ci_isources[slot]->is_recurse = stubp->ist_recurse;
		setgate(&idt[idt_vec], stubp->ist_entry, 0, SDT_SYS386IGT,
		    SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	}

	pic->pic_addroute(pic, ci, pin, idt_vec, type);

	if (!cold)
		pic->pic_hwunmask(pic, pin);

#ifdef INTRDEBUG
	printf("allocated pic %s type %s pin %d level %d to cpu%u slot %d idt entry %d\n",
	    pic->pic_name, type == IST_EDGE ? "edge" : "level", pin, level,
	    ci->ci_apicid, slot, idt_vec);
#endif

	return (ih);
}

/*
 * Deregister an interrupt handler.
 */
void
intr_disestablish(struct intrhand *ih)
{
	struct intrhand **p, *q;
	struct cpu_info *ci;
	struct pic *pic;
	struct intrsource *source;
	int idtvec;

	ci = ih->ih_cpu;
	pic = ci->ci_isources[ih->ih_slot]->is_pic;
	source = ci->ci_isources[ih->ih_slot];
	idtvec = source->is_idtvec;

	pic->pic_hwmask(pic, ih->ih_pin);	
	x86_atomic_clearbits_u64(&ci->ci_ipending, (1UL << ih->ih_slot));

	/*
	 * Remove the handler from the chain.
	 */
	for (p = &source->is_handlers; (q = *p) != NULL && q != ih;
	     p = &q->ih_next)
		;
	if (q == NULL) {
		panic("intr_disestablish: handler not registered");
	}

	*p = q->ih_next;

	intr_calculatemasks(ci);
	if (source->is_handlers == NULL)
		pic->pic_delroute(pic, ci, ih->ih_pin, idtvec, source->is_type);
	else
		pic->pic_hwunmask(pic, ih->ih_pin);

#ifdef INTRDEBUG
	printf("cpu%u: remove slot %d (pic %s pin %d vec %d)\n",
	    ci->ci_apicid, ih->ih_slot, pic->pic_dev.dv_xname, ih->ih_pin,
	    idtvec);
#endif

	if (source->is_handlers == NULL) {
		free(source, M_DEVBUF, sizeof (struct intrsource));
		ci->ci_isources[ih->ih_slot] = NULL;
		if (pic != &i8259_pic)
			idt_vec_free(idtvec);
	}

	evcount_detach(&ih->ih_count);
	free(ih, M_DEVBUF, sizeof(*ih));
}

int
intr_handler(struct intrframe *frame, struct intrhand *ih)
{
	struct cpu_info *ci = curcpu();
	int floor;
	int rc;
#ifdef MULTIPROCESSOR
	int need_lock;
#endif

	/*
	 * We may not be able to mask MSIs, so block non-wakeup
	 * interrupts while we're suspended.
	 */
	if (intr_suspended && (ih->ih_flags & IPL_WAKEUP) == 0) {
		intr_nowake = ih;
		return 0;
	}

#ifdef MULTIPROCESSOR
	if (ih->ih_flags & IPL_MPSAFE)
		need_lock = 0;
	else
		need_lock = 1;

	if (need_lock)
		__mp_lock(&kernel_lock);
#endif
	floor = ci->ci_handled_intr_level;
	ci->ci_handled_intr_level = ih->ih_level;
	rc = (*ih->ih_fun)(ih->ih_arg ? ih->ih_arg : frame);
	ci->ci_handled_intr_level = floor;
#ifdef MULTIPROCESSOR
	if (need_lock)
		__mp_unlock(&kernel_lock);
#endif
	return rc;
}

/*
 * Fake interrupt handler structures for the benefit of symmetry with
 * other interrupt sources, and the benefit of intr_calculatemasks()
 */
struct intrhand fake_softclock_intrhand;
struct intrhand fake_softnet_intrhand;
struct intrhand fake_softtty_intrhand;
struct intrhand fake_timer_intrhand;
struct intrhand fake_ipi_intrhand;
#if NXEN > 0
struct intrhand fake_xen_intrhand;
#endif
#if NHYPERV > 0
struct intrhand fake_hyperv_intrhand;
#endif

/*
 * Initialize all handlers that aren't dynamically allocated, and exist
 * for each CPU.
 */
void
cpu_intr_init(struct cpu_info *ci)
{
	struct intrsource *isp;
#if NLAPIC > 0 && defined(MULTIPROCESSOR) && 0
	int i;
#endif

	isp = malloc(sizeof (struct intrsource), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (isp == NULL)
		panic("can't allocate fixed interrupt source");
	isp->is_recurse = Xsoftclock;
	isp->is_resume = Xsoftclock;
	fake_softclock_intrhand.ih_level = IPL_SOFTCLOCK;
	isp->is_handlers = &fake_softclock_intrhand;
	isp->is_pic = &softintr_pic;
	ci->ci_isources[SIR_CLOCK] = isp;
	isp = malloc(sizeof (struct intrsource), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (isp == NULL)
		panic("can't allocate fixed interrupt source");
	isp->is_recurse = Xsoftnet;
	isp->is_resume = Xsoftnet;
	fake_softnet_intrhand.ih_level = IPL_SOFTNET;
	isp->is_handlers = &fake_softnet_intrhand;
	isp->is_pic = &softintr_pic;
	ci->ci_isources[SIR_NET] = isp;
	isp = malloc(sizeof (struct intrsource), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (isp == NULL)
		panic("can't allocate fixed interrupt source");
	isp->is_recurse = Xsofttty;
	isp->is_resume = Xsofttty;
	fake_softtty_intrhand.ih_level = IPL_SOFTTTY;
	isp->is_handlers = &fake_softtty_intrhand;
	isp->is_pic = &softintr_pic;
	ci->ci_isources[SIR_TTY] = isp;
#if NLAPIC > 0
	isp = malloc(sizeof (struct intrsource), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (isp == NULL)
		panic("can't allocate fixed interrupt source");
	isp->is_recurse = Xrecurse_lapic_ltimer;
	isp->is_resume = Xresume_lapic_ltimer;
	fake_timer_intrhand.ih_level = IPL_CLOCK;
	isp->is_handlers = &fake_timer_intrhand;
	isp->is_pic = &local_pic;
	ci->ci_isources[LIR_TIMER] = isp;
#ifdef MULTIPROCESSOR
	isp = malloc(sizeof (struct intrsource), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (isp == NULL)
		panic("can't allocate fixed interrupt source");
	isp->is_recurse = Xrecurse_lapic_ipi;
	isp->is_resume = Xresume_lapic_ipi;
	fake_ipi_intrhand.ih_level = IPL_IPI;
	isp->is_handlers = &fake_ipi_intrhand;
	isp->is_pic = &local_pic;
	ci->ci_isources[LIR_IPI] = isp;
#endif
#if NXEN > 0
	isp = malloc(sizeof (struct intrsource), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (isp == NULL)
		panic("can't allocate fixed interrupt source");
	isp->is_recurse = Xrecurse_xen_upcall;
	isp->is_resume = Xresume_xen_upcall;
	fake_xen_intrhand.ih_level = IPL_NET;
	isp->is_handlers = &fake_xen_intrhand;
	isp->is_pic = &local_pic;
	ci->ci_isources[LIR_XEN] = isp;
#endif
#if NHYPERV > 0
	isp = malloc(sizeof (struct intrsource), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (isp == NULL)
		panic("can't allocate fixed interrupt source");
	isp->is_recurse = Xrecurse_hyperv_upcall;
	isp->is_resume = Xresume_hyperv_upcall;
	fake_hyperv_intrhand.ih_level = IPL_NET;
	isp->is_handlers = &fake_hyperv_intrhand;
	isp->is_pic = &local_pic;
	ci->ci_isources[LIR_HYPERV] = isp;
#endif
#endif	/* NLAPIC */

	intr_calculatemasks(ci);

}

void
intr_printconfig(void)
{
#ifdef INTRDEBUG
	int i;
	struct intrhand *ih;
	struct intrsource *isp;
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		printf("cpu%d: interrupt masks:\n", ci->ci_apicid);
		for (i = 0; i < NIPL; i++)
			printf("IPL %d mask %lx unmask %lx\n", i,
			    (u_long)ci->ci_imask[i], (u_long)ci->ci_iunmask[i]);
		for (i = 0; i < MAX_INTR_SOURCES; i++) {
			isp = ci->ci_isources[i];
			if (isp == NULL)
				continue;
			printf("cpu%u source %d is pin %d from pic %s maxlevel %d\n",
			    ci->ci_apicid, i, isp->is_pin,
			    isp->is_pic->pic_name, isp->is_maxlevel);
			for (ih = isp->is_handlers; ih != NULL;
			     ih = ih->ih_next)
				printf("\thandler %p level %d\n",
				    ih->ih_fun, ih->ih_level);

		}
	}
#endif
}

void
intr_barrier(void *cookie)
{
	struct intrhand *ih = cookie;
	sched_barrier(ih->ih_cpu);
}

void
intr_set_wakeup(void *cookie)
{
	struct intrhand *ih = cookie;
	ih->ih_flags |= IPL_WAKEUP;
}

#ifdef SUSPEND

void
intr_enable_wakeup(void)
{
	struct cpu_info *ci = curcpu();
	struct pic *pic;
	int irq, pin;

	for (irq = 0; irq < MAX_INTR_SOURCES; irq++) {
		if (ci->ci_isources[irq] == NULL)
			continue;

		if (ci->ci_isources[irq]->is_handlers->ih_flags & IPL_WAKEUP)
			continue;

		pic = ci->ci_isources[irq]->is_pic;
		pin = ci->ci_isources[irq]->is_pin;
		if (pic->pic_hwmask)
			pic->pic_hwmask(pic, pin);
	}

	intr_suspended = 1;
}

void
intr_disable_wakeup(void)
{
	struct cpu_info *ci = curcpu();
	struct pic *pic;
	int irq, pin;

	intr_suspended = 0;

	for (irq = 0; irq < MAX_INTR_SOURCES; irq++) {
		if (ci->ci_isources[irq] == NULL)
			continue;

		if (ci->ci_isources[irq]->is_handlers->ih_flags & IPL_WAKEUP)
			continue;

		pic = ci->ci_isources[irq]->is_pic;
		pin = ci->ci_isources[irq]->is_pin;
		if (pic->pic_hwunmask)
			pic->pic_hwunmask(pic, pin);
	}

	if (intr_nowake) {
		printf("last non-wakeup interrupt: irq%d/%s\n",
		    *(int *)intr_nowake->ih_count.ec_data,
		    intr_nowake->ih_count.ec_name);
		intr_nowake = NULL;
	}
}

#endif

/*
 * Add a mask to cpl, and return the old value of cpl.
 */
int
splraise(int nlevel)
{
	int olevel;
	struct cpu_info *ci = curcpu();

	KASSERT(nlevel >= IPL_NONE);

	olevel = ci->ci_ilevel;
	ci->ci_ilevel = MAX(ci->ci_ilevel, nlevel);
	return (olevel);
}

/*
 * Restore a value to cpl (unmasking interrupts).  If any unmasked
 * interrupts are pending, call Xspllower() to process them.
 */
int
spllower(int nlevel)
{
	int olevel;
	struct cpu_info *ci = curcpu();
	u_int64_t imask;
	u_long flags;

	imask = IUNMASK(ci, nlevel);
	olevel = ci->ci_ilevel;

	flags = intr_disable();

	if (ci->ci_ipending & imask) {
		Xspllower(nlevel);
	} else {
		ci->ci_ilevel = nlevel;
		intr_restore(flags);
	}
	return (olevel);
}

/*
 * Software interrupt registration
 *
 * We hand-code this to ensure that it's atomic.
 */
void
softintr(int si_level)
{
	struct cpu_info *ci = curcpu();
	int sir = softintr_to_ssir[si_level];

	__asm volatile("lock; orq %1, %0" :
	    "=m"(ci->ci_ipending) : "ir" (1UL << sir));
}

void
dosoftint(int si_level)
{
	struct cpu_info *ci = curcpu();
	int floor;

	floor = ci->ci_handled_intr_level;
	ci->ci_handled_intr_level = ci->ci_ilevel;

	softintr_dispatch(si_level);

	ci->ci_handled_intr_level = floor;
}
