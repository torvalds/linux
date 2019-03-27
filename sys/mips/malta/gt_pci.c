/*	$NetBSD: gt_pci.c,v 1.4 2003/07/15 00:24:54 lukem Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001, 2002 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
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

/*
 * PCI configuration support for gt I/O Processor chip.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <mips/malta/maltareg.h>

#include <mips/malta/gtreg.h>
#include <mips/malta/gtvar.h>

#include <isa/isareg.h>
#include <dev/ic/i8259.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/pci/pcib_private.h>
#include "pcib_if.h"

#include <mips/malta/gt_pci_bus_space.h>

#define	ICU_LEN		16	/* number of ISA IRQs */

/*
 * XXX: These defines are from NetBSD's <dev/ic/i8259reg.h>. Respective file
 * from FreeBSD src tree <dev/ic/i8259.h> lacks some definitions.
 */
#define PIC_OCW1	1
#define PIC_OCW2	0
#define PIC_OCW3	0

#define OCW2_SELECT	0
#define OCW2_ILS(x)     ((x) << 0)      /* interrupt level select */

#define OCW3_POLL_IRQ(x) ((x) & 0x7f)
#define OCW3_POLL_PENDING (1U << 7)

/*
 * Galileo controller's registers are LE so convert to then
 * to/from native byte order. We rely on boot loader or emulator
 * to set "swap bytes" configuration correctly for us
 */
#define	GT_PCI_DATA(v)	htole32((v))
#define	GT_HOST_DATA(v)	le32toh((v))

struct gt_pci_softc;

struct gt_pci_intr_cookie {
	int irq;
	struct gt_pci_softc *sc;
};

struct gt_pci_softc {
	device_t 		sc_dev;
	bus_space_tag_t 	sc_st;
	bus_space_handle_t	sc_ioh_icu1;
	bus_space_handle_t	sc_ioh_icu2;
	bus_space_handle_t	sc_ioh_elcr;

	int			sc_busno;
	struct rman		sc_mem_rman;
	struct rman		sc_io_rman;
	struct rman		sc_irq_rman;
	unsigned long		sc_mem;
	bus_space_handle_t	sc_io;

	struct resource		*sc_irq;
	struct intr_event	*sc_eventstab[ICU_LEN];
	struct gt_pci_intr_cookie	sc_intr_cookies[ICU_LEN];
	uint16_t		sc_imask;
	uint16_t		sc_elcr;

	uint16_t		sc_reserved;

	void			*sc_ih;
};

static void gt_pci_set_icus(struct gt_pci_softc *);
static int gt_pci_intr(void *v);
static int gt_pci_probe(device_t);
static int gt_pci_attach(device_t);
static int gt_pci_activate_resource(device_t, device_t, int, int, 
    struct resource *);
static int gt_pci_setup_intr(device_t, device_t, struct resource *, 
    int, driver_filter_t *, driver_intr_t *, void *, void **);
static int gt_pci_teardown_intr(device_t, device_t, struct resource *, void*);
static int gt_pci_maxslots(device_t );
static int gt_pci_conf_setup(struct gt_pci_softc *, int, int, int, int, 
    uint32_t *);
static uint32_t gt_pci_read_config(device_t, u_int, u_int, u_int, u_int, int);
static void gt_pci_write_config(device_t, u_int, u_int, u_int, u_int, 
    uint32_t, int);
static int gt_pci_route_interrupt(device_t pcib, device_t dev, int pin);
static struct resource * gt_pci_alloc_resource(device_t, device_t, int, 
    int *, rman_res_t, rman_res_t, rman_res_t, u_int);

static void
gt_pci_mask_irq(void *source)
{
	struct gt_pci_intr_cookie *cookie = source;
	struct gt_pci_softc *sc = cookie->sc;
	int irq = cookie->irq;

	sc->sc_imask |= (1 << irq);
	sc->sc_elcr |= (1 << irq);

	gt_pci_set_icus(sc);
}

static void
gt_pci_unmask_irq(void *source)
{
	struct gt_pci_intr_cookie *cookie = source;
	struct gt_pci_softc *sc = cookie->sc;
	int irq = cookie->irq;

	/* Enable it, set trigger mode. */
	sc->sc_imask &= ~(1 << irq);
	sc->sc_elcr &= ~(1 << irq);

	gt_pci_set_icus(sc);
}

static void
gt_pci_set_icus(struct gt_pci_softc *sc)
{
	/* Enable the cascade IRQ (2) if 8-15 is enabled. */
	if ((sc->sc_imask & 0xff00) != 0xff00)
		sc->sc_imask &= ~(1U << 2);
	else
		sc->sc_imask |= (1U << 2);

	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu1, PIC_OCW1,
	    sc->sc_imask & 0xff);
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu2, PIC_OCW1,
	    (sc->sc_imask >> 8) & 0xff);

	bus_space_write_1(sc->sc_st, sc->sc_ioh_elcr, 0,
	    sc->sc_elcr & 0xff);
	bus_space_write_1(sc->sc_st, sc->sc_ioh_elcr, 1,
	    (sc->sc_elcr >> 8) & 0xff);
}

static int
gt_pci_intr(void *v)
{
	struct gt_pci_softc *sc = v;
	struct intr_event *event;
	int irq;

	for (;;) {
		bus_space_write_1(sc->sc_st, sc->sc_ioh_icu1, PIC_OCW3,
		    OCW3_SEL | OCW3_P);
		irq = bus_space_read_1(sc->sc_st, sc->sc_ioh_icu1, PIC_OCW3);
		if ((irq & OCW3_POLL_PENDING) == 0)
		{
			return FILTER_HANDLED;
		}

		irq = OCW3_POLL_IRQ(irq);

		if (irq == 2) {
			bus_space_write_1(sc->sc_st, sc->sc_ioh_icu2,
			    PIC_OCW3, OCW3_SEL | OCW3_P);
			irq = bus_space_read_1(sc->sc_st, sc->sc_ioh_icu2,
			    PIC_OCW3);
			if (irq & OCW3_POLL_PENDING)
				irq = OCW3_POLL_IRQ(irq) + 8;
			else
				irq = 2;
		}

		event = sc->sc_eventstab[irq];

		if (!event || CK_SLIST_EMPTY(&event->ie_handlers))
			continue;

		/* TODO: frame instead of NULL? */
		intr_event_handle(event, NULL);
		/* XXX: Log stray IRQs */

		/* Send a specific EOI to the 8259. */
		if (irq > 7) {
			bus_space_write_1(sc->sc_st, sc->sc_ioh_icu2,
			    PIC_OCW2, OCW2_SELECT | OCW2_EOI | OCW2_SL |
			    OCW2_ILS(irq & 7));
			irq = 2;
		}

		bus_space_write_1(sc->sc_st, sc->sc_ioh_icu1, PIC_OCW2,
		    OCW2_SELECT | OCW2_EOI | OCW2_SL | OCW2_ILS(irq));
	}

	return FILTER_HANDLED;
}

static int
gt_pci_probe(device_t dev)
{
	device_set_desc(dev, "GT64120 PCI bridge");
	return (0);
}

static int
gt_pci_attach(device_t dev)
{

	uint32_t busno;				       
	struct gt_pci_softc *sc = device_get_softc(dev);
	int rid;

	busno = 0;
	sc->sc_dev = dev;
	sc->sc_busno = busno;
	sc->sc_st = mips_bus_space_generic;

	/* Use KSEG1 to access IO ports for it is uncached */
	sc->sc_io = MALTA_PCI0_IO_BASE;
	sc->sc_io_rman.rm_type = RMAN_ARRAY;
	sc->sc_io_rman.rm_descr = "GT64120 PCI I/O Ports";
	/* 
	 * First 256 bytes are ISA's registers: e.g. i8259's
	 * So do not use them for general purpose PCI I/O window
	 */
	if (rman_init(&sc->sc_io_rman) != 0 ||
	    rman_manage_region(&sc->sc_io_rman, 0x100, 0xffff) != 0) {
		panic("gt_pci_attach: failed to set up I/O rman");
	}

	/* Use KSEG1 to access PCI memory for it is uncached */
	sc->sc_mem = MALTA_PCIMEM1_BASE;
	sc->sc_mem_rman.rm_type = RMAN_ARRAY;
	sc->sc_mem_rman.rm_descr = "GT64120 PCI Memory";
	if (rman_init(&sc->sc_mem_rman) != 0 ||
	    rman_manage_region(&sc->sc_mem_rman, 
	    sc->sc_mem, sc->sc_mem + MALTA_PCIMEM1_SIZE) != 0) {
		panic("gt_pci_attach: failed to set up memory rman");
	}
	sc->sc_irq_rman.rm_type = RMAN_ARRAY;
	sc->sc_irq_rman.rm_descr = "GT64120 PCI IRQs";
	if (rman_init(&sc->sc_irq_rman) != 0 ||
	    rman_manage_region(&sc->sc_irq_rman, 1, 31) != 0)
		panic("gt_pci_attach: failed to set up IRQ rman");

	/*
	 * Map the PIC/ELCR registers.
	 */
#if 0
	if (bus_space_map(sc->sc_st, 0x4d0, 2, 0, &sc->sc_ioh_elcr) != 0)
		device_printf(dev, "unable to map ELCR registers\n");
	if (bus_space_map(sc->sc_st, IO_ICU1, 2, 0, &sc->sc_ioh_icu1) != 0)
		device_printf(dev, "unable to map ICU1 registers\n");
	if (bus_space_map(sc->sc_st, IO_ICU2, 2, 0, &sc->sc_ioh_icu2) != 0)
		device_printf(dev, "unable to map ICU2 registers\n");
#else
	sc->sc_ioh_elcr = MIPS_PHYS_TO_KSEG1(sc->sc_io + 0x4d0);
	sc->sc_ioh_icu1 = MIPS_PHYS_TO_KSEG1(sc->sc_io + IO_ICU1);
	sc->sc_ioh_icu2 = MIPS_PHYS_TO_KSEG1(sc->sc_io + IO_ICU2);
#endif	


	/* All interrupts default to "masked off". */
	sc->sc_imask = 0xffff;

	/* All interrupts default to edge-triggered. */
	sc->sc_elcr = 0;

	/*
	 * Initialize the 8259s.
	 */
	/* reset, program device, 4 bytes */
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu1, 0,
	    ICW1_RESET | ICW1_IC4);
	/*
	 * XXX: values from NetBSD's <dev/ic/i8259reg.h>
	 */	 
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu1, 1,
	    0/*XXX*/);
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu1, 1,
	    1 << 2);
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu1, 1,
	    ICW4_8086);

	/* mask all interrupts */
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu1, 1,
	    sc->sc_imask & 0xff);

	/* enable special mask mode */
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu1, 0,
	    OCW3_SEL | OCW3_ESMM | OCW3_SMM);

	/* read IRR by default */
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu1, 0,
	    OCW3_SEL | OCW3_RR);

	/* reset, program device, 4 bytes */
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu2, 0,
	    ICW1_RESET | ICW1_IC4);
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu2, 1,
	    0/*XXX*/);
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu2, 1,
	    1 << 2);
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu2, 1,
	    ICW4_8086);

	/* mask all interrupts */
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu2, 1,
	    sc->sc_imask & 0xff);

	/* enable special mask mode */
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu2, 0,
	    OCW3_SEL | OCW3_ESMM | OCW3_SMM);

	/* read IRR by default */
	bus_space_write_1(sc->sc_st, sc->sc_ioh_icu2, 0,
	    OCW3_SEL | OCW3_RR);

	/*
	 * Default all interrupts to edge-triggered.
	 */
	bus_space_write_1(sc->sc_st, sc->sc_ioh_elcr, 0,
	    sc->sc_elcr & 0xff);
	bus_space_write_1(sc->sc_st, sc->sc_ioh_elcr, 1,
	    (sc->sc_elcr >> 8) & 0xff);

	/*
	 * Some ISA interrupts are reserved for devices that
	 * we know are hard-wired to certain IRQs.
	 */
	sc->sc_reserved =
		(1U << 0) |     /* timer */
		(1U << 1) |     /* keyboard controller (keyboard) */
		(1U << 2) |     /* PIC cascade */
		(1U << 3) |     /* COM 2 */
		(1U << 4) |     /* COM 1 */
		(1U << 6) |     /* floppy */
		(1U << 7) |     /* centronics */
		(1U << 8) |     /* RTC */
		(1U << 9) |	/* I2C */
		(1U << 12) |    /* keyboard controller (mouse) */
		(1U << 14) |    /* IDE primary */
		(1U << 15);     /* IDE secondary */

	/* Hook up our interrupt handler. */
	if ((sc->sc_irq = bus_alloc_resource(dev, SYS_RES_IRQ, &rid, 
	    MALTA_SOUTHBRIDGE_INTR, MALTA_SOUTHBRIDGE_INTR, 1, 
	    RF_SHAREABLE | RF_ACTIVE)) == NULL) {
		device_printf(dev, "unable to allocate IRQ resource\n");
		return ENXIO;
	}

	if ((bus_setup_intr(dev, sc->sc_irq, INTR_TYPE_MISC,
			    gt_pci_intr, NULL, sc, &sc->sc_ih))) {
		device_printf(dev, 
		    "WARNING: unable to register interrupt handler\n");
		return ENXIO;
	}

	/* Initialize memory and i/o rmans. */
	device_add_child(dev, "pci", -1);
	return (bus_generic_attach(dev));
}

static int
gt_pci_maxslots(device_t dev)
{
	return (PCI_SLOTMAX);
}

static int
gt_pci_conf_setup(struct gt_pci_softc *sc, int bus, int slot, int func,
    int reg, uint32_t *addr)
{
	*addr = (bus << 16) | (slot << 11) | (func << 8) | reg;

	return (0);
}

static uint32_t
gt_pci_read_config(device_t dev, u_int bus, u_int slot, u_int func, u_int reg,
    int bytes)
{
	struct gt_pci_softc *sc = device_get_softc(dev);
	uint32_t data;
	uint32_t addr;
	uint32_t shift, mask;

	if (gt_pci_conf_setup(sc, bus, slot, func, reg & ~3, &addr))
		return (uint32_t)(-1);

	/* Clear cause register bits. */
	GT_REGVAL(GT_INTR_CAUSE) = GT_PCI_DATA(0);
	GT_REGVAL(GT_PCI0_CFG_ADDR) = GT_PCI_DATA((1U << 31) | addr);
	/* 
	 * Galileo system controller is special
	 */
	if ((bus == 0) && (slot == 0))
		data = GT_PCI_DATA(GT_REGVAL(GT_PCI0_CFG_DATA));
	else
		data = GT_REGVAL(GT_PCI0_CFG_DATA);

	/* Check for master abort. */
	if (GT_HOST_DATA(GT_REGVAL(GT_INTR_CAUSE)) & (GTIC_MASABORT0 | GTIC_TARABORT0))
		data = (uint32_t) -1;

	switch(reg % 4)
	{
	case 3:
		shift = 24;
		break;
	case 2:
		shift = 16;
		break;
	case 1:
		shift = 8;
		break;
	default:
		shift = 0;
		break;
	}	

	switch(bytes)
	{
	case 1:
		mask = 0xff;
		data = (data >> shift) & mask;
		break;
	case 2:
		mask = 0xffff;
		if(reg % 4 == 0)
			data = data & mask;
		else
			data = (data >> 16) & mask;
		break;
	case 4:
		break;
	default:
		panic("gt_pci_readconfig: wrong bytes count");
		break;
	}
#if 0
	printf("PCICONF_READ(%02x:%02x.%02x[%04x] -> %02x(%d)\n", 
	  bus, slot, func, reg, data, bytes);
#endif

	return (data);
}

static void
gt_pci_write_config(device_t dev, u_int bus, u_int slot, u_int func, u_int reg,
    uint32_t data, int bytes)
{
	struct gt_pci_softc *sc = device_get_softc(dev);
	uint32_t addr;
	uint32_t reg_data;
	uint32_t shift, mask;

	if(bytes != 4)
	{
		reg_data = gt_pci_read_config(dev, bus, slot, func, reg, 4);

		shift = 8 * (reg & 3);

		switch(bytes)
		{
		case 1:
			mask = 0xff;
			data = (reg_data & ~ (mask << shift)) | (data << shift);
			break;
		case 2:
			mask = 0xffff;
			if(reg % 4 == 0)
				data = (reg_data & ~mask) | data;
			else
				data = (reg_data & ~ (mask << shift)) | 
				    (data << shift);
			break;
		case 4:
			break;
		default:
			panic("gt_pci_readconfig: wrong bytes count");
			break;
		}
	}

	if (gt_pci_conf_setup(sc, bus, slot, func, reg & ~3, &addr))
		return;

	/* The galileo has problems accessing device 31. */
	if (bus == 0 && slot == 31)
		return;

	/* XXX: no support for bus > 0 yet */
	if (bus > 0)
		return;

	/* Clear cause register bits. */
	GT_REGVAL(GT_INTR_CAUSE) = GT_PCI_DATA(0);

	GT_REGVAL(GT_PCI0_CFG_ADDR) = GT_PCI_DATA((1U << 31) | addr);

	/* 
	 * Galileo system controller is special
	 */
	if ((bus == 0) && (slot == 0))
		GT_REGVAL(GT_PCI0_CFG_DATA) = GT_PCI_DATA(data);
	else
		GT_REGVAL(GT_PCI0_CFG_DATA) = data;

#if 0
	printf("PCICONF_WRITE(%02x:%02x.%02x[%04x] -> %02x(%d)\n", 
	  bus, slot, func, reg, data, bytes);
#endif

}

static int
gt_pci_route_interrupt(device_t pcib, device_t dev, int pin)
{
	int bus;
	int device;
	int func;
	/* struct gt_pci_softc *sc = device_get_softc(pcib); */
	bus = pci_get_bus(dev);
	device = pci_get_slot(dev);
	func = pci_get_function(dev);
	/* 
	 * XXXMIPS: We need routing logic. This is just a stub .
	 */
	switch (device) {
	case 9: /*
		 * PIIX4 IDE adapter. HW IRQ0
		 */
		return 0;
	case 11: /* Ethernet */
		return 10;
	default:
		device_printf(pcib, "no IRQ mapping for %d/%d/%d/%d\n", bus, device, func, pin);
		
	}
	return (0);

}

static int
gt_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct gt_pci_softc *sc = device_get_softc(dev);
	switch (which) {
	case PCIB_IVAR_DOMAIN:
		*result = 0;
		return (0);
	case PCIB_IVAR_BUS:
		*result = sc->sc_busno;
		return (0);
		
	}
	return (ENOENT);
}

static int
gt_write_ivar(device_t dev, device_t child, int which, uintptr_t result)
{
	struct gt_pci_softc * sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_BUS:
		sc->sc_busno = result;
		return (0);
	}
	return (ENOENT);
}

static struct resource *
gt_pci_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct gt_pci_softc *sc = device_get_softc(bus);	
	struct resource *rv = NULL;
	struct rman *rm;
	bus_space_handle_t bh = 0;

	switch (type) {
	case SYS_RES_IRQ:
		rm = &sc->sc_irq_rman;
		break;
	case SYS_RES_MEMORY:
		rm = &sc->sc_mem_rman;
		bh = sc->sc_mem;
		break;
	case SYS_RES_IOPORT:
		rm = &sc->sc_io_rman;
		bh = sc->sc_io;
		break;
	default:
		return (NULL);
	}

	rv = rman_reserve_resource(rm, start, end, count, flags, child);
	if (rv == NULL)
		return (NULL);
	rman_set_rid(rv, *rid);
	if (type != SYS_RES_IRQ) {
		bh += (rman_get_start(rv));

		rman_set_bustag(rv, gt_pci_bus_space);
		rman_set_bushandle(rv, bh);
		if (flags & RF_ACTIVE) {
			if (bus_activate_resource(child, type, *rid, rv)) {
				rman_release_resource(rv);
				return (NULL);
			}
		} 
	}
	return (rv);
}

static int
gt_pci_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	bus_space_handle_t p;
	int error;
	
	if ((type == SYS_RES_MEMORY) || (type == SYS_RES_IOPORT)) {
		error = bus_space_map(rman_get_bustag(r),
		    rman_get_bushandle(r), rman_get_size(r), 0, &p);
		if (error) 
			return (error);
		rman_set_bushandle(r, p);
	}
	return (rman_activate_resource(r));
}

static int
gt_pci_setup_intr(device_t dev, device_t child, struct resource *ires, 
		int flags, driver_filter_t *filt, driver_intr_t *handler, 
		void *arg, void **cookiep)
{
	struct gt_pci_softc *sc = device_get_softc(dev);
	struct intr_event *event;
	int irq, error;

	irq = rman_get_start(ires);
	if (irq >= ICU_LEN || irq == 2)
		panic("%s: bad irq or type", __func__);

	event = sc->sc_eventstab[irq];
	sc->sc_intr_cookies[irq].irq = irq;
	sc->sc_intr_cookies[irq].sc = sc;
	if (event == NULL) {
                error = intr_event_create(&event, 
		    (void *)&sc->sc_intr_cookies[irq], 0, irq,
		    gt_pci_mask_irq, gt_pci_unmask_irq,
		    NULL, NULL, "gt_pci intr%d:", irq);
		if (error)
			return 0;
		sc->sc_eventstab[irq] = event;
	}

	intr_event_add_handler(event, device_get_nameunit(child), filt, 
	    handler, arg, intr_priority(flags), flags, cookiep);

	gt_pci_unmask_irq((void *)&sc->sc_intr_cookies[irq]);
	return 0;
}

static int
gt_pci_teardown_intr(device_t dev, device_t child, struct resource *res,
    void *cookie)
{
	struct gt_pci_softc *sc = device_get_softc(dev);
	int irq;

	irq = rman_get_start(res);
	gt_pci_mask_irq((void *)&sc->sc_intr_cookies[irq]);

	return (intr_event_remove_handler(cookie));
}

static device_method_t gt_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		gt_pci_probe),
	DEVMETHOD(device_attach,	gt_pci_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	gt_read_ivar),
	DEVMETHOD(bus_write_ivar,	gt_write_ivar),
	DEVMETHOD(bus_alloc_resource,	gt_pci_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, gt_pci_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	gt_pci_setup_intr),
	DEVMETHOD(bus_teardown_intr,	gt_pci_teardown_intr),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	gt_pci_maxslots),
	DEVMETHOD(pcib_read_config,	gt_pci_read_config),
	DEVMETHOD(pcib_write_config,	gt_pci_write_config),
	DEVMETHOD(pcib_route_interrupt,	gt_pci_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	DEVMETHOD_END
};

static driver_t gt_pci_driver = {
	"pcib",
	gt_pci_methods,
	sizeof(struct gt_pci_softc),
};

static devclass_t gt_pci_devclass;

DRIVER_MODULE(gt_pci, gt, gt_pci_driver, gt_pci_devclass, 0, 0);
