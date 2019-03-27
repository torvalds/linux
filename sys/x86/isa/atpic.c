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
 */

/*
 * PIC driver for the 8259A Master and Slave PICs in PC/AT machines.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_auto_eoi.h"
#include "opt_isa.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>

#include <machine/cpufunc.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/resource.h>
#include <machine/segments.h>

#include <dev/ic/i8259.h>
#include <x86/isa/icu.h>
#include <isa/isareg.h>
#include <isa/isavar.h>

#ifdef __amd64__
#define	SDT_ATPIC	SDT_SYSIGT
#define	GSEL_ATPIC	0
#else
#define	SDT_ATPIC	SDT_SYS386IGT
#define	GSEL_ATPIC	GSEL(GCODE_SEL, SEL_KPL)
#endif

#define	MASTER	0
#define	SLAVE	1

#define	IMEN_MASK(ai)		(IRQ_MASK((ai)->at_irq))

#define	NUM_ISA_IRQS		16

static void	atpic_init(void *dummy);

inthand_t
	IDTVEC(atpic_intr0), IDTVEC(atpic_intr1), IDTVEC(atpic_intr2),
	IDTVEC(atpic_intr3), IDTVEC(atpic_intr4), IDTVEC(atpic_intr5),
	IDTVEC(atpic_intr6), IDTVEC(atpic_intr7), IDTVEC(atpic_intr8),
	IDTVEC(atpic_intr9), IDTVEC(atpic_intr10), IDTVEC(atpic_intr11),
	IDTVEC(atpic_intr12), IDTVEC(atpic_intr13), IDTVEC(atpic_intr14),
	IDTVEC(atpic_intr15);
/* XXXKIB i386 uses stubs until pti comes */
inthand_t
	IDTVEC(atpic_intr0_pti), IDTVEC(atpic_intr1_pti),
	IDTVEC(atpic_intr2_pti), IDTVEC(atpic_intr3_pti),
	IDTVEC(atpic_intr4_pti), IDTVEC(atpic_intr5_pti),
	IDTVEC(atpic_intr6_pti), IDTVEC(atpic_intr7_pti),
	IDTVEC(atpic_intr8_pti), IDTVEC(atpic_intr9_pti),
	IDTVEC(atpic_intr10_pti), IDTVEC(atpic_intr11_pti),
	IDTVEC(atpic_intr12_pti), IDTVEC(atpic_intr13_pti),
	IDTVEC(atpic_intr14_pti), IDTVEC(atpic_intr15_pti);

#define	IRQ(ap, ai)	((ap)->at_irqbase + (ai)->at_irq)

#define	ATPIC(io, base, eoi) {						\
		.at_pic = {						\
			.pic_register_sources = atpic_register_sources,	\
			.pic_enable_source = atpic_enable_source,	\
			.pic_disable_source = atpic_disable_source,	\
			.pic_eoi_source = (eoi),			\
			.pic_enable_intr = atpic_enable_intr,		\
			.pic_disable_intr = atpic_disable_intr,		\
			.pic_vector = atpic_vector,			\
			.pic_source_pending = atpic_source_pending,	\
			.pic_resume = atpic_resume,			\
			.pic_config_intr = atpic_config_intr,		\
			.pic_assign_cpu = atpic_assign_cpu		\
		},							\
		.at_ioaddr = (io),					\
		.at_irqbase = (base),					\
		.at_intbase = IDT_IO_INTS + (base),			\
		.at_imen = 0xff,					\
	}

#define	INTSRC(irq)							\
	{ { &atpics[(irq) / 8].at_pic }, IDTVEC(atpic_intr ## irq ),	\
	    IDTVEC(atpic_intr ## irq ## _pti), (irq) % 8 }

struct atpic {
	struct pic at_pic;
	int	at_ioaddr;
	int	at_irqbase;
	uint8_t	at_intbase;
	uint8_t	at_imen;
};

struct atpic_intsrc {
	struct intsrc at_intsrc;
	inthand_t *at_intr, *at_intr_pti;
	int	at_irq;			/* Relative to PIC base. */
	enum intr_trigger at_trigger;
	u_long	at_count;
	u_long	at_straycount;
};

static void atpic_register_sources(struct pic *pic);
static void atpic_enable_source(struct intsrc *isrc);
static void atpic_disable_source(struct intsrc *isrc, int eoi);
static void atpic_eoi_master(struct intsrc *isrc);
static void atpic_eoi_slave(struct intsrc *isrc);
static void atpic_enable_intr(struct intsrc *isrc);
static void atpic_disable_intr(struct intsrc *isrc);
static int atpic_vector(struct intsrc *isrc);
static void atpic_resume(struct pic *pic, bool suspend_cancelled);
static int atpic_source_pending(struct intsrc *isrc);
static int atpic_config_intr(struct intsrc *isrc, enum intr_trigger trig,
    enum intr_polarity pol);
static int atpic_assign_cpu(struct intsrc *isrc, u_int apic_id);
static void i8259_init(struct atpic *pic, int slave);

static struct atpic atpics[] = {
	ATPIC(IO_ICU1, 0, atpic_eoi_master),
	ATPIC(IO_ICU2, 8, atpic_eoi_slave)
};

static struct atpic_intsrc atintrs[] = {
	INTSRC(0),
	INTSRC(1),
	INTSRC(2),
	INTSRC(3),
	INTSRC(4),
	INTSRC(5),
	INTSRC(6),
	INTSRC(7),
	INTSRC(8),
	INTSRC(9),
	INTSRC(10),
	INTSRC(11),
	INTSRC(12),
	INTSRC(13),
	INTSRC(14),
	INTSRC(15),
};

CTASSERT(nitems(atintrs) == NUM_ISA_IRQS);

static __inline void
_atpic_eoi_master(struct intsrc *isrc)
{

	KASSERT(isrc->is_pic == &atpics[MASTER].at_pic,
	    ("%s: mismatched pic", __func__));
#ifndef AUTO_EOI_1
	outb(atpics[MASTER].at_ioaddr, OCW2_EOI);
#endif
}

/*
 * The data sheet says no auto-EOI on slave, but it sometimes works.
 * So, if AUTO_EOI_2 is enabled, we use it.
 */
static __inline void
_atpic_eoi_slave(struct intsrc *isrc)
{

	KASSERT(isrc->is_pic == &atpics[SLAVE].at_pic,
	    ("%s: mismatched pic", __func__));
#ifndef AUTO_EOI_2
	outb(atpics[SLAVE].at_ioaddr, OCW2_EOI);
#ifndef AUTO_EOI_1
	outb(atpics[MASTER].at_ioaddr, OCW2_EOI);
#endif
#endif
}

static void
atpic_register_sources(struct pic *pic)
{
	struct atpic *ap = (struct atpic *)pic;
	struct atpic_intsrc *ai;
	int i;

	/*
	 * If any of the ISA IRQs have an interrupt source already, then
	 * assume that the I/O APICs are being used and don't register any
	 * of our interrupt sources.  This makes sure we don't accidentally
	 * use mixed mode.  The "accidental" use could otherwise occur on
	 * machines that route the ACPI SCI interrupt to a different ISA
	 * IRQ (at least one machine routes it to IRQ 13) thus disabling
	 * that APIC ISA routing and allowing the ATPIC source for that IRQ
	 * to leak through.  We used to depend on this feature for routing
	 * IRQ0 via mixed mode, but now we don't use mixed mode at all.
	 *
	 * To avoid the slave not register sources after the master
	 * registers its sources, register all IRQs when this function is
	 * called on the master.
	 */
	if (ap != &atpics[MASTER])
		return;
	for (i = 0; i < NUM_ISA_IRQS; i++)
		if (intr_lookup_source(i) != NULL)
			return;

	/* Loop through all interrupt sources and add them. */
	for (i = 0, ai = atintrs; i < NUM_ISA_IRQS; i++, ai++) {
		if (i == ICU_SLAVEID)
			continue;
		intr_register_source(&ai->at_intsrc);
	}
}

static void
atpic_enable_source(struct intsrc *isrc)
{
	struct atpic_intsrc *ai = (struct atpic_intsrc *)isrc;
	struct atpic *ap = (struct atpic *)isrc->is_pic;

	spinlock_enter();
	if (ap->at_imen & IMEN_MASK(ai)) {
		ap->at_imen &= ~IMEN_MASK(ai);
		outb(ap->at_ioaddr + ICU_IMR_OFFSET, ap->at_imen);
	}
	spinlock_exit();
}

static void
atpic_disable_source(struct intsrc *isrc, int eoi)
{
	struct atpic_intsrc *ai = (struct atpic_intsrc *)isrc;
	struct atpic *ap = (struct atpic *)isrc->is_pic;

	spinlock_enter();
	if (ai->at_trigger != INTR_TRIGGER_EDGE) {
		ap->at_imen |= IMEN_MASK(ai);
		outb(ap->at_ioaddr + ICU_IMR_OFFSET, ap->at_imen);
	}

	/*
	 * Take care to call these functions directly instead of through
	 * a function pointer.  All of the referenced variables should
	 * still be hot in the cache.
	 */
	if (eoi == PIC_EOI) {
		if (isrc->is_pic == &atpics[MASTER].at_pic)
			_atpic_eoi_master(isrc);
		else
			_atpic_eoi_slave(isrc);
	}

	spinlock_exit();
}

static void
atpic_eoi_master(struct intsrc *isrc)
{
#ifndef AUTO_EOI_1
	spinlock_enter();
	_atpic_eoi_master(isrc);
	spinlock_exit();
#endif
}

static void
atpic_eoi_slave(struct intsrc *isrc)
{
#ifndef AUTO_EOI_2
	spinlock_enter();
	_atpic_eoi_slave(isrc);
	spinlock_exit();
#endif
}

static void
atpic_enable_intr(struct intsrc *isrc)
{
}

static void
atpic_disable_intr(struct intsrc *isrc)
{
}


static int
atpic_vector(struct intsrc *isrc)
{
	struct atpic_intsrc *ai = (struct atpic_intsrc *)isrc;
	struct atpic *ap = (struct atpic *)isrc->is_pic;

	return (IRQ(ap, ai));
}

static int
atpic_source_pending(struct intsrc *isrc)
{
	struct atpic_intsrc *ai = (struct atpic_intsrc *)isrc;
	struct atpic *ap = (struct atpic *)isrc->is_pic;

	return (inb(ap->at_ioaddr) & IMEN_MASK(ai));
}

static void
atpic_resume(struct pic *pic, bool suspend_cancelled)
{
	struct atpic *ap = (struct atpic *)pic;

	i8259_init(ap, ap == &atpics[SLAVE]);
	if (ap == &atpics[SLAVE] && elcr_found)
		elcr_resume();
}

static int
atpic_config_intr(struct intsrc *isrc, enum intr_trigger trig,
    enum intr_polarity pol)
{
	struct atpic_intsrc *ai = (struct atpic_intsrc *)isrc;
	u_int vector;

	/* Map conforming values to edge/hi and sanity check the values. */
	if (trig == INTR_TRIGGER_CONFORM)
		trig = INTR_TRIGGER_EDGE;
	if (pol == INTR_POLARITY_CONFORM)
		pol = INTR_POLARITY_HIGH;
	vector = atpic_vector(isrc);
	if ((trig == INTR_TRIGGER_EDGE && pol == INTR_POLARITY_LOW) ||
	    (trig == INTR_TRIGGER_LEVEL && pol == INTR_POLARITY_HIGH)) {
		printf(
		"atpic: Mismatched config for IRQ%u: trigger %s, polarity %s\n",
		    vector, trig == INTR_TRIGGER_EDGE ? "edge" : "level",
		    pol == INTR_POLARITY_HIGH ? "high" : "low");
		return (EINVAL);
	}

	/* If there is no change, just return. */
	if (ai->at_trigger == trig)
		return (0);

	/*
	 * Certain IRQs can never be level/lo, so don't try to set them
	 * that way if asked.  At least some ELCR registers ignore setting
	 * these bits as well.
	 */
	if ((vector == 0 || vector == 1 || vector == 2 || vector == 13) &&
	    trig == INTR_TRIGGER_LEVEL) {
		if (bootverbose)
			printf(
		"atpic: Ignoring invalid level/low configuration for IRQ%u\n",
			    vector);
		return (EINVAL);
	}
	if (!elcr_found) {
		if (bootverbose)
			printf("atpic: No ELCR to configure IRQ%u as %s\n",
			    vector, trig == INTR_TRIGGER_EDGE ? "edge/high" :
			    "level/low");
		return (ENXIO);
	}
	if (bootverbose)
		printf("atpic: Programming IRQ%u as %s\n", vector,
		    trig == INTR_TRIGGER_EDGE ? "edge/high" : "level/low");
	spinlock_enter();
	elcr_write_trigger(atpic_vector(isrc), trig);
	ai->at_trigger = trig;
	spinlock_exit();
	return (0);
}

static int
atpic_assign_cpu(struct intsrc *isrc, u_int apic_id)
{

	/*
	 * 8259A's are only used in UP in which case all interrupts always
	 * go to the sole CPU and this function shouldn't even be called.
	 */
	panic("%s: bad cookie", __func__);
}

static void
i8259_init(struct atpic *pic, int slave)
{
	int imr_addr;

	/* Reset the PIC and program with next four bytes. */
	spinlock_enter();
	outb(pic->at_ioaddr, ICW1_RESET | ICW1_IC4);
	imr_addr = pic->at_ioaddr + ICU_IMR_OFFSET;

	/* Start vector. */
	outb(imr_addr, pic->at_intbase);

	/*
	 * Setup slave links.  For the master pic, indicate what line
	 * the slave is configured on.  For the slave indicate
	 * which line on the master we are connected to.
	 */
	if (slave)
		outb(imr_addr, ICU_SLAVEID);
	else
		outb(imr_addr, IRQ_MASK(ICU_SLAVEID));

	/* Set mode. */
	if (slave)
		outb(imr_addr, SLAVE_MODE);
	else
		outb(imr_addr, MASTER_MODE);

	/* Set interrupt enable mask. */
	outb(imr_addr, pic->at_imen);

	/* Reset is finished, default to IRR on read. */
	outb(pic->at_ioaddr, OCW3_SEL | OCW3_RR);

	/* OCW2_L1 sets priority order to 3-7, 0-2 (com2 first). */
	if (!slave)
		outb(pic->at_ioaddr, OCW2_R | OCW2_SL | OCW2_L1);

	spinlock_exit();
}

void
atpic_startup(void)
{
	struct atpic_intsrc *ai;
	int i;

	/* Start off with all interrupts disabled. */
	i8259_init(&atpics[MASTER], 0);
	i8259_init(&atpics[SLAVE], 1);
	atpic_enable_source((struct intsrc *)&atintrs[ICU_SLAVEID]);

	/* Install low-level interrupt handlers for all of our IRQs. */
	for (i = 0, ai = atintrs; i < NUM_ISA_IRQS; i++, ai++) {
		if (i == ICU_SLAVEID)
			continue;
		ai->at_intsrc.is_count = &ai->at_count;
		ai->at_intsrc.is_straycount = &ai->at_straycount;
		setidt(((struct atpic *)ai->at_intsrc.is_pic)->at_intbase +
		    ai->at_irq, pti ? ai->at_intr_pti : ai->at_intr, SDT_ATPIC,
		    SEL_KPL, GSEL_ATPIC);
	}

	/*
	 * Look for an ELCR.  If we find one, update the trigger modes.
	 * If we don't find one, assume that IRQs 0, 1, 2, and 13 are
	 * edge triggered and that everything else is level triggered.
	 * We only use the trigger information to reprogram the ELCR if
	 * we have one and as an optimization to avoid masking edge
	 * triggered interrupts.  For the case that we don't have an ELCR,
	 * it doesn't hurt to mask an edge triggered interrupt, so we
	 * assume level trigger for any interrupt that we aren't sure is
	 * edge triggered.
	 */
	if (elcr_found) {
		for (i = 0, ai = atintrs; i < NUM_ISA_IRQS; i++, ai++)
			ai->at_trigger = elcr_read_trigger(i);
	} else {
		for (i = 0, ai = atintrs; i < NUM_ISA_IRQS; i++, ai++)
			switch (i) {
			case 0:
			case 1:
			case 2:
			case 8:
			case 13:
				ai->at_trigger = INTR_TRIGGER_EDGE;
				break;
			default:
				ai->at_trigger = INTR_TRIGGER_LEVEL;
				break;
			}
	}
}

static void
atpic_init(void *dummy __unused)
{

	/*
	 * Register our PICs, even if we aren't going to use any of their
	 * pins so that they are suspended and resumed.
	 */
	if (intr_register_pic(&atpics[0].at_pic) != 0 ||
	    intr_register_pic(&atpics[1].at_pic) != 0)
		panic("Unable to register ATPICs");

	if (num_io_irqs == 0)
		num_io_irqs = NUM_ISA_IRQS;
}
SYSINIT(atpic_init, SI_SUB_INTR, SI_ORDER_FOURTH, atpic_init, NULL);

void
atpic_handle_intr(u_int vector, struct trapframe *frame)
{
	struct intsrc *isrc;

	KASSERT(vector < NUM_ISA_IRQS, ("unknown int %u\n", vector));
	isrc = &atintrs[vector].at_intsrc;

	/*
	 * If we don't have an event, see if this is a spurious
	 * interrupt.
	 */
	if (isrc->is_event == NULL && (vector == 7 || vector == 15)) {
		int port, isr;

		/*
		 * Read the ISR register to see if IRQ 7/15 is really
		 * pending.  Reset read register back to IRR when done.
		 */
		port = ((struct atpic *)isrc->is_pic)->at_ioaddr;
		spinlock_enter();
		outb(port, OCW3_SEL | OCW3_RR | OCW3_RIS);
		isr = inb(port);
		outb(port, OCW3_SEL | OCW3_RR);
		spinlock_exit();
		if ((isr & IRQ_MASK(7)) == 0)
			return;
	}
	intr_execute_handlers(isrc, frame);
}

#ifdef DEV_ISA
/*
 * Bus attachment for the ISA PIC.
 */
static struct isa_pnp_id atpic_ids[] = {
	{ 0x0000d041 /* PNP0000 */, "AT interrupt controller" },
	{ 0 }
};

static int
atpic_probe(device_t dev)
{
	int result;
	
	result = ISA_PNP_PROBE(device_get_parent(dev), dev, atpic_ids);
	if (result <= 0)
		device_quiet(dev);
	return (result);
}

/*
 * We might be granted IRQ 2, as this is typically consumed by chaining
 * between the two PIC components.  If we're using the APIC, however,
 * this may not be the case, and as such we should free the resource.
 * (XXX untested)
 *
 * The generic ISA attachment code will handle allocating any other resources
 * that we don't explicitly claim here.
 */
static int
atpic_attach(device_t dev)
{
	struct resource *res;
	int rid;

	/* Try to allocate our IRQ and then free it. */
	rid = 0;
	res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, 0);
	if (res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, rid, res);
	return (0);
}

/*
 * Return a bitmap of the current interrupt requests.  This is 8259-specific
 * and is only suitable for use at probe time.
 */
intrmask_t
isa_irq_pending(void)
{
	u_char irr1;
	u_char irr2;

	irr1 = inb(IO_ICU1);
	irr2 = inb(IO_ICU2);
	return ((irr2 << 8) | irr1);
}

static device_method_t atpic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		atpic_probe),
	DEVMETHOD(device_attach,	atpic_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	{ 0, 0 }
};

static driver_t atpic_driver = {
	"atpic",
	atpic_methods,
	1,		/* no softc */
};

static devclass_t atpic_devclass;

DRIVER_MODULE(atpic, isa, atpic_driver, atpic_devclass, 0, 0);
DRIVER_MODULE(atpic, acpi, atpic_driver, atpic_devclass, 0, 0);
ISA_PNP_INFO(atpic_ids);
#endif /* DEV_ISA */
