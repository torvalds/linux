/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/cpuinfo.h>
#include <machine/cpuregs.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/trap.h>
#include <machine/hwfunc.h>

#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/interrupt.h>
#include <mips/nlm/hal/pic.h>
#include <mips/nlm/xlp.h>

struct xlp_intrsrc {
	void (*bus_ack)(int, void *);	/* Additional ack */
	void *bus_ack_arg;		/* arg for additional ack */
	struct intr_event *ie;		/* event corresponding to intr */
	int irq;
	int irt;
};

static struct xlp_intrsrc xlp_interrupts[XLR_MAX_INTR];
static mips_intrcnt_t mips_intr_counters[XLR_MAX_INTR];
static int intrcnt_index;

int
xlp_irq_to_irt(int irq)
{
	uint32_t offset;

	switch (irq) {
	case PIC_UART_0_IRQ:
	case PIC_UART_1_IRQ:
		offset =  XLP_IO_UART_OFFSET(0, irq - PIC_UART_0_IRQ);
		return (xlp_socdev_irt(offset));
	case PIC_PCIE_0_IRQ:
	case PIC_PCIE_1_IRQ:
	case PIC_PCIE_2_IRQ:
	case PIC_PCIE_3_IRQ:
		offset = XLP_IO_PCIE_OFFSET(0, irq - PIC_PCIE_0_IRQ);
		return (xlp_socdev_irt(offset));
	case PIC_USB_0_IRQ:
	case PIC_USB_1_IRQ:
	case PIC_USB_2_IRQ:
	case PIC_USB_3_IRQ:
	case PIC_USB_4_IRQ:
		offset = XLP_IO_USB_OFFSET(0, irq - PIC_USB_0_IRQ);
		return (xlp_socdev_irt(offset));
	case PIC_I2C_0_IRQ:
	case PIC_I2C_1_IRQ:
		offset = XLP_IO_I2C0_OFFSET(0);
		return (xlp_socdev_irt(offset) + irq - PIC_I2C_0_IRQ);
	default:
		printf("ERROR: %s: unknown irq %d\n", __func__, irq);
		return (-1);
	}
}

void
xlp_enable_irq(int irq)
{
	uint64_t eimr;

	eimr = nlm_read_c0_eimr();
	nlm_write_c0_eimr(eimr | (1ULL << irq));
}

void
cpu_establish_softintr(const char *name, driver_filter_t * filt,
    void (*handler) (void *), void *arg, int irq, int flags,
    void **cookiep)
{

	panic("Soft interrupts unsupported!\n");
}

static void
xlp_post_filter(void *source)
{
	struct xlp_intrsrc *src = source;

	if (src->bus_ack)
		src->bus_ack(src->irq, src->bus_ack_arg);
	nlm_pic_ack(xlp_pic_base, src->irt);
}

static void
xlp_pre_ithread(void *source)
{
	struct xlp_intrsrc *src = source;

	if (src->bus_ack)
		src->bus_ack(src->irq, src->bus_ack_arg);
}

static void
xlp_post_ithread(void *source)
{
	struct xlp_intrsrc *src = source;

	nlm_pic_ack(xlp_pic_base, src->irt);
}

void
xlp_set_bus_ack(int irq, void (*ack)(int, void *), void *arg)
{
	struct xlp_intrsrc *src;

	KASSERT(irq > 0 && irq <= XLR_MAX_INTR,
	    ("%s called for bad hard intr %d", __func__, irq));

	/* no locking needed - this will called early in boot */
	src = &xlp_interrupts[irq];
	KASSERT(src->ie != NULL,
	    ("%s called after IRQ enable for %d.", __func__, irq));
	src->bus_ack_arg = arg;
	src->bus_ack = ack;
}

void
cpu_establish_hardintr(const char *name, driver_filter_t * filt,
    void (*handler) (void *), void *arg, int irq, int flags,
    void **cookiep)
{
	struct intr_event *ie;	/* descriptor for the IRQ */
	struct xlp_intrsrc *src = NULL;
	int errcode;

	KASSERT(irq > 0 && irq <= XLR_MAX_INTR ,
	    ("%s called for bad hard intr %d", __func__, irq));

	/*
	 * Locking - not needed now, because we do this only on
	 * startup from CPU0
	 */
	src = &xlp_interrupts[irq];
	ie = src->ie;
	if (ie == NULL) {
		/*
		 * PIC based interrupts need ack in PIC, and some SoC
		 * components need additional acks (e.g. PCI)
		 */
		if (XLP_IRQ_IS_PICINTR(irq))
			errcode = intr_event_create(&ie, src, 0, irq,
			    xlp_pre_ithread, xlp_post_ithread, xlp_post_filter,
			    NULL, "hard intr%d:", irq);
		else {
			if (filt == NULL)
				panic("Unsupported non filter percpu intr %d", irq);
			errcode = intr_event_create(&ie, src, 0, irq,
			    NULL, NULL, NULL, NULL, "hard intr%d:", irq);
		}
		if (errcode) {
			printf("Could not create event for intr %d\n", irq);
			return;
		}
		src->irq = irq;
		src->ie = ie;
	}
	if (XLP_IRQ_IS_PICINTR(irq)) {
		/* Set all irqs to CPU 0 for now */
		src->irt = xlp_irq_to_irt(irq);
		nlm_pic_write_irt_direct(xlp_pic_base, src->irt, 1, 0,
		    PIC_LOCAL_SCHEDULING, irq, 0);
	}

	intr_event_add_handler(ie, name, filt, handler, arg,
	    intr_priority(flags), flags, cookiep);
	xlp_enable_irq(irq);
}

void
cpu_intr(struct trapframe *tf)
{
	struct intr_event *ie;
	uint64_t eirr, eimr;
	int i;

	critical_enter();

	/* find a list of enabled interrupts */
	eirr = nlm_read_c0_eirr();
	eimr = nlm_read_c0_eimr();
	eirr &= eimr;

	if (eirr == 0) {
		critical_exit();
		return;
	}
	/*
	 * No need to clear the EIRR here as the handler writes to
	 * compare which ACKs the interrupt.
	 */
	if (eirr & (1 << IRQ_TIMER)) {
		intr_event_handle(xlp_interrupts[IRQ_TIMER].ie, tf);
		critical_exit();
		return;
	}

	/* FIXME sched pin >? LOCK>? */
	for (i = sizeof(eirr) * 8 - 1; i >= 0; i--) {
		if ((eirr & (1ULL << i)) == 0)
			continue;

		ie = xlp_interrupts[i].ie;
		/* Don't account special IRQs */
		switch (i) {
		case IRQ_IPI:
		case IRQ_MSGRING:
			break;
		default:
			mips_intrcnt_inc(mips_intr_counters[i]);
		}

		/* Ack the IRQ on the CPU */
		nlm_write_c0_eirr(1ULL << i);
		if (intr_event_handle(ie, tf) != 0) {
			printf("stray interrupt %d\n", i);
		}
	}
	critical_exit();
}

void
mips_intrcnt_setname(mips_intrcnt_t counter, const char *name)
{
	int idx = counter - intrcnt;

	KASSERT(counter != NULL, ("mips_intrcnt_setname: NULL counter"));

	snprintf(intrnames + (MAXCOMLEN + 1) * idx,
	    MAXCOMLEN + 1, "%-*s", MAXCOMLEN, name);
}

mips_intrcnt_t
mips_intrcnt_create(const char* name)
{
	mips_intrcnt_t counter = &intrcnt[intrcnt_index++];

	mips_intrcnt_setname(counter, name);
	return counter;
}

void
cpu_init_interrupts()
{
	int i;
	char name[MAXCOMLEN + 1];

	/*
	 * Initialize all available vectors so spare IRQ
	 * would show up in systat output
	 */
	for (i = 0; i < XLR_MAX_INTR; i++) {
		snprintf(name, MAXCOMLEN + 1, "int%d:", i);
		mips_intr_counters[i] = mips_intrcnt_create(name);
	}
}

static int	xlp_pic_probe(device_t);
static int	xlp_pic_attach(device_t);

static int
xlp_pic_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "netlogic,xlp-pic"))
		return (ENXIO);
	device_set_desc(dev, "XLP PIC");
	return (0);
}

static int
xlp_pic_attach(device_t dev)
{

	return (0);
}

static device_method_t xlp_pic_methods[] = {
	DEVMETHOD(device_probe,		xlp_pic_probe),
	DEVMETHOD(device_attach,	xlp_pic_attach),

	DEVMETHOD_END
};

static driver_t xlp_pic_driver = {
	"xlp_pic",
	xlp_pic_methods,
	1,		/* no softc */
};

static devclass_t xlp_pic_devclass;
DRIVER_MODULE(xlp_pic, simplebus, xlp_pic_driver, xlp_pic_devclass, 0, 0);
