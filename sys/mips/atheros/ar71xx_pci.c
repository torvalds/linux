/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ar71xx.h"

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr_machdep.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/pci/pcib_private.h>
#include "pcib_if.h"

#include <mips/atheros/ar71xxreg.h>
#include <mips/atheros/ar71xx_pci_bus_space.h>

#include <mips/atheros/ar71xx_cpudef.h>

#ifdef	AR71XX_ATH_EEPROM
#include <mips/atheros/ar71xx_fixup.h>
#endif	/* AR71XX_ATH_EEPROM */

#undef	AR71XX_PCI_DEBUG
#ifdef	AR71XX_PCI_DEBUG
#define	dprintf printf
#else
#define	dprintf(x, arg...)
#endif

struct mtx ar71xx_pci_mtx;
MTX_SYSINIT(ar71xx_pci_mtx, &ar71xx_pci_mtx, "ar71xx PCI space mutex",
    MTX_SPIN);

struct ar71xx_pci_softc {
	device_t		sc_dev;

	int			sc_busno;
	int			sc_baseslot;
	struct rman		sc_mem_rman;
	struct rman		sc_irq_rman;

	struct intr_event	*sc_eventstab[AR71XX_PCI_NIRQS];	
	mips_intrcnt_t		sc_intr_counter[AR71XX_PCI_NIRQS];	
	struct resource		*sc_irq;
	void			*sc_ih;
};

static int ar71xx_pci_setup_intr(device_t, device_t, struct resource *, int, 
		    driver_filter_t *, driver_intr_t *, void *, void **);
static int ar71xx_pci_teardown_intr(device_t, device_t, struct resource *,
		    void *);
static int ar71xx_pci_intr(void *);

static void
ar71xx_pci_mask_irq(void *source)
{
	uint32_t reg;
	unsigned int irq = (unsigned int)source;

	/* XXX is the PCI lock required here? */
	reg = ATH_READ_REG(AR71XX_PCI_INTR_MASK);
	/* flush */
	reg = ATH_READ_REG(AR71XX_PCI_INTR_MASK);
	ATH_WRITE_REG(AR71XX_PCI_INTR_MASK, reg & ~(1 << irq));
}

static void
ar71xx_pci_unmask_irq(void *source)
{
	uint32_t reg;
	unsigned int irq = (unsigned int)source;

	/* XXX is the PCI lock required here? */
	reg = ATH_READ_REG(AR71XX_PCI_INTR_MASK);
	ATH_WRITE_REG(AR71XX_PCI_INTR_MASK, reg | (1 << irq));
	/* flush */
	reg = ATH_READ_REG(AR71XX_PCI_INTR_MASK);
}

/*
 * get bitmask for bytes of interest:
 *   0 - we want this byte, 1 - ignore it. e.g: we read 1 byte
 *   from register 7. Bitmask would be: 0111
 */
static uint32_t
ar71xx_get_bytes_to_read(int reg, int bytes)
{
	uint32_t bytes_to_read = 0;

	if ((bytes % 4) == 0)
		bytes_to_read = 0;
	else if ((bytes % 4) == 1)
		bytes_to_read = (~(1 << (reg % 4))) & 0xf;
	else if ((bytes % 4) == 2)
		bytes_to_read = (~(3 << (reg % 4))) & 0xf;
	else
		panic("%s: wrong combination", __func__);

	return (bytes_to_read);
}

static int
ar71xx_pci_check_bus_error(void)
{
	uint32_t error, addr, has_errors = 0;

	mtx_assert(&ar71xx_pci_mtx, MA_OWNED);

	error = ATH_READ_REG(AR71XX_PCI_ERROR) & 0x3;
	dprintf("%s: PCI error = %02x\n", __func__, error);
	if (error) {
		addr = ATH_READ_REG(AR71XX_PCI_ERROR_ADDR);

		/* Do not report it yet */
#if 0
		printf("PCI bus error %d at addr 0x%08x\n", error, addr);
#endif
		ATH_WRITE_REG(AR71XX_PCI_ERROR, error);
		has_errors = 1;
	}

	error = ATH_READ_REG(AR71XX_PCI_AHB_ERROR) & 0x1;
	dprintf("%s: AHB error = %02x\n", __func__, error);
	if (error) {
		addr = ATH_READ_REG(AR71XX_PCI_AHB_ERROR_ADDR);
		/* Do not report it yet */
#if 0
		printf("AHB bus error %d at addr 0x%08x\n", error, addr);
#endif
		ATH_WRITE_REG(AR71XX_PCI_AHB_ERROR, error);
		has_errors = 1;
	}

	return (has_errors);
}

static uint32_t
ar71xx_pci_make_addr(int bus, int slot, int func, int reg)
{
	if (bus == 0) {
		return ((1 << slot) | (func << 8) | (reg & ~3));
	} else {
		return ((bus << 16) | (slot << 11) | (func << 8)
		    | (reg  & ~3) | 1);
	}
}

static int
ar71xx_pci_conf_setup(int bus, int slot, int func, int reg, int bytes,
    uint32_t cmd)
{
	uint32_t addr = ar71xx_pci_make_addr(bus, slot, func, (reg & ~3));

	mtx_assert(&ar71xx_pci_mtx, MA_OWNED);

	cmd |= (ar71xx_get_bytes_to_read(reg, bytes) << 4);
	ATH_WRITE_REG(AR71XX_PCI_CONF_ADDR, addr);
	ATH_WRITE_REG(AR71XX_PCI_CONF_CMD, cmd);

	dprintf("%s: tag (%x, %x, %x) %d/%d addr=%08x, cmd=%08x\n", __func__, 
	    bus, slot, func, reg, bytes, addr, cmd);

	return ar71xx_pci_check_bus_error();
}

static uint32_t
ar71xx_pci_read_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, int bytes)
{
	uint32_t data;
	uint32_t shift, mask;

	/* register access is 32-bit aligned */
	shift = (reg & 3) * 8;

	/* Create a mask based on the width, post-shift */
	if (bytes == 2)
		mask = 0xffff;
	else if (bytes == 1)
		mask = 0xff;
	else
		mask = 0xffffffff;

	dprintf("%s: tag (%x, %x, %x) reg %d(%d)\n", __func__, bus, slot, 
	    func, reg, bytes);

	mtx_lock_spin(&ar71xx_pci_mtx);
	 if (ar71xx_pci_conf_setup(bus, slot, func, reg, bytes, 
	     PCI_CONF_CMD_READ) == 0)
		 data = ATH_READ_REG(AR71XX_PCI_CONF_READ_DATA);
	 else
		 data = -1;
	mtx_unlock_spin(&ar71xx_pci_mtx);

	/* get request bytes from 32-bit word */
	data = (data >> shift) & mask;

	dprintf("%s: read 0x%x\n", __func__, data);

	return (data);
}

static void
ar71xx_pci_local_write(device_t dev, uint32_t reg, uint32_t data, int bytes)
{
	uint32_t cmd;

	dprintf("%s: local write reg %d(%d)\n", __func__, reg, bytes);

	data = data << (8*(reg % 4));
	cmd = PCI_LCONF_CMD_WRITE | (reg & ~3);
	cmd |= (ar71xx_get_bytes_to_read(reg, bytes) << 20);
	mtx_lock_spin(&ar71xx_pci_mtx);
	ATH_WRITE_REG(AR71XX_PCI_LCONF_CMD, cmd);
	ATH_WRITE_REG(AR71XX_PCI_LCONF_WRITE_DATA, data);
	mtx_unlock_spin(&ar71xx_pci_mtx);
}

static void
ar71xx_pci_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, uint32_t data, int bytes)
{

	dprintf("%s: tag (%x, %x, %x) reg %d(%d)\n", __func__, bus, slot,
	    func, reg, bytes);

	data = data << (8*(reg % 4));
	mtx_lock_spin(&ar71xx_pci_mtx);
	 if (ar71xx_pci_conf_setup(bus, slot, func, reg, bytes,
	     PCI_CONF_CMD_WRITE) == 0)
		 ATH_WRITE_REG(AR71XX_PCI_CONF_WRITE_DATA, data);
	mtx_unlock_spin(&ar71xx_pci_mtx);
}

#ifdef	AR71XX_ATH_EEPROM
/*
 * Some embedded boards (eg AP94) have the MAC attached via PCI but they
 * don't have the MAC-attached EEPROM.  The register initialisation
 * values and calibration data are stored in the on-board flash.
 * This routine initialises the NIC via the EEPROM register contents
 * before the probe/attach routines get a go at things.
 */
static void
ar71xx_pci_fixup(device_t dev, u_int bus, u_int slot, u_int func,
    long flash_addr, int len)
{
	uint16_t *cal_data = (uint16_t *) MIPS_PHYS_TO_KSEG1(flash_addr);
	uint32_t reg, val, bar0;

	if (bootverbose)
		device_printf(dev, "%s: flash_addr=%lx, cal_data=%p\n",
		    __func__, flash_addr, cal_data);

	/* XXX check 0xa55a */
	/* Save bar(0) address - just to flush bar(0) (SoC WAR) ? */
	bar0 = ar71xx_pci_read_config(dev, bus, slot, func, PCIR_BAR(0), 4);
	ar71xx_pci_write_config(dev, bus, slot, func, PCIR_BAR(0),
	    AR71XX_PCI_MEM_BASE, 4);

	val = ar71xx_pci_read_config(dev, bus, slot, func, PCIR_COMMAND, 2);
	val |= (PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN);
	ar71xx_pci_write_config(dev, bus, slot, func, PCIR_COMMAND, val, 2); 

	cal_data += 3;
	while (*cal_data != 0xffff) {
		reg = *cal_data++;
		val = *cal_data++;
		val |= (*cal_data++) << 16;
		if (bootverbose)
			printf("  reg: %x, val=%x\n", reg, val);

		/* Write eeprom fixup data to device memory */
		ATH_WRITE_REG(AR71XX_PCI_MEM_BASE + reg, val);
		DELAY(100);
	}

	val = ar71xx_pci_read_config(dev, bus, slot, func, PCIR_COMMAND, 2);
	val &= ~(PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN);
	ar71xx_pci_write_config(dev, bus, slot, func, PCIR_COMMAND, val, 2);

	/* Write the saved bar(0) address */
	ar71xx_pci_write_config(dev, bus, slot, func, PCIR_BAR(0), bar0, 4);
}

static void
ar71xx_pci_slot_fixup(device_t dev, u_int bus, u_int slot, u_int func)
{
	long int flash_addr;
	char buf[64];
	int size;

	/*
	 * Check whether the given slot has a hint to poke.
	 */
	if (bootverbose)
	device_printf(dev, "%s: checking dev %s, %d/%d/%d\n",
	    __func__, device_get_nameunit(dev), bus, slot, func);

	snprintf(buf, sizeof(buf), "bus.%d.%d.%d.ath_fixup_addr",
	    bus, slot, func);

	if (resource_long_value(device_get_name(dev), device_get_unit(dev),
	    buf, &flash_addr) == 0) {
		snprintf(buf, sizeof(buf), "bus.%d.%d.%d.ath_fixup_size",
		    bus, slot, func);
		if (resource_int_value(device_get_name(dev),
		    device_get_unit(dev), buf, &size) != 0) {
			device_printf(dev,
			    "%s: missing hint '%s', aborting EEPROM\n",
			    __func__, buf);
			return;
		}


		device_printf(dev, "found EEPROM at 0x%lx on %d.%d.%d\n",
		    flash_addr, bus, slot, func);
		ar71xx_pci_fixup(dev, bus, slot, func, flash_addr, size);
		ar71xx_pci_slot_create_eeprom_firmware(dev, bus, slot, func,
		    flash_addr, size);
	}
}
#endif	/* AR71XX_ATH_EEPROM */

static int
ar71xx_pci_probe(device_t dev)
{

	return (BUS_PROBE_NOWILDCARD);
}

static int
ar71xx_pci_attach(device_t dev)
{
	int rid = 0;
	struct ar71xx_pci_softc *sc = device_get_softc(dev);

	sc->sc_mem_rman.rm_type = RMAN_ARRAY;
	sc->sc_mem_rman.rm_descr = "ar71xx PCI memory window";
	if (rman_init(&sc->sc_mem_rman) != 0 || 
	    rman_manage_region(&sc->sc_mem_rman, AR71XX_PCI_MEM_BASE, 
		AR71XX_PCI_MEM_BASE + AR71XX_PCI_MEM_SIZE - 1) != 0) {
		panic("ar71xx_pci_attach: failed to set up I/O rman");
	}

	sc->sc_irq_rman.rm_type = RMAN_ARRAY;
	sc->sc_irq_rman.rm_descr = "ar71xx PCI IRQs";
	if (rman_init(&sc->sc_irq_rman) != 0 ||
	    rman_manage_region(&sc->sc_irq_rman, AR71XX_PCI_IRQ_START, 
	        AR71XX_PCI_IRQ_END) != 0)
		panic("ar71xx_pci_attach: failed to set up IRQ rman");

	/*
	 * Check if there is a base slot hint. Otherwise use default value.
	 */
	if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "baseslot", &sc->sc_baseslot) != 0) {
		device_printf(dev,
		    "%s: missing hint '%s', default to AR71XX_PCI_BASE_SLOT\n",
		    __func__, "baseslot");
		sc->sc_baseslot = AR71XX_PCI_BASE_SLOT;
	}

	ATH_WRITE_REG(AR71XX_PCI_INTR_STATUS, 0);
	ATH_WRITE_REG(AR71XX_PCI_INTR_MASK, 0);

	/* Hook up our interrupt handler. */
	if ((sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE)) == NULL) {
		device_printf(dev, "unable to allocate IRQ resource\n");
		return ENXIO;
	}

	if ((bus_setup_intr(dev, sc->sc_irq, INTR_TYPE_MISC,
			    ar71xx_pci_intr, NULL, sc, &sc->sc_ih))) {
		device_printf(dev,
		    "WARNING: unable to register interrupt handler\n");
		return ENXIO;
	}

	/* reset PCI core and PCI bus */
	ar71xx_device_stop(RST_RESET_PCI_CORE | RST_RESET_PCI_BUS);
	DELAY(100000);

	ar71xx_device_start(RST_RESET_PCI_CORE | RST_RESET_PCI_BUS);
	DELAY(100000);

	/* Init PCI windows */
	ATH_WRITE_REG(AR71XX_PCI_WINDOW0, PCI_WINDOW0_ADDR);
	ATH_WRITE_REG(AR71XX_PCI_WINDOW1, PCI_WINDOW1_ADDR);
	ATH_WRITE_REG(AR71XX_PCI_WINDOW2, PCI_WINDOW2_ADDR);
	ATH_WRITE_REG(AR71XX_PCI_WINDOW3, PCI_WINDOW3_ADDR);
	ATH_WRITE_REG(AR71XX_PCI_WINDOW4, PCI_WINDOW4_ADDR);
	ATH_WRITE_REG(AR71XX_PCI_WINDOW5, PCI_WINDOW5_ADDR);
	ATH_WRITE_REG(AR71XX_PCI_WINDOW6, PCI_WINDOW6_ADDR);
	ATH_WRITE_REG(AR71XX_PCI_WINDOW7, PCI_WINDOW7_CONF_ADDR);
	DELAY(100000);

	mtx_lock_spin(&ar71xx_pci_mtx);
	ar71xx_pci_check_bus_error();
	mtx_unlock_spin(&ar71xx_pci_mtx);

	/* Fixup internal PCI bridge */
	ar71xx_pci_local_write(dev, PCIR_COMMAND,
            PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN
	    | PCIM_CMD_SERRESPEN | PCIM_CMD_BACKTOBACK
	    | PCIM_CMD_PERRESPEN | PCIM_CMD_MWRICEN, 4);

#ifdef	AR71XX_ATH_EEPROM
	/*
	 * Hard-code a check for slot 17 and 18 - these are
	 * the two PCI slots which may have a PCI device that
	 * requires "fixing".
	 */
	ar71xx_pci_slot_fixup(dev, 0, 17, 0);
	ar71xx_pci_slot_fixup(dev, 0, 18, 0);
#endif	/* AR71XX_ATH_EEPROM */

	device_add_child(dev, "pci", -1);
	return (bus_generic_attach(dev));
}

static int
ar71xx_pci_read_ivar(device_t dev, device_t child, int which,
    uintptr_t *result)
{
	struct ar71xx_pci_softc *sc = device_get_softc(dev);

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
ar71xx_pci_write_ivar(device_t dev, device_t child, int which,
    uintptr_t result)
{
	struct ar71xx_pci_softc * sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_BUS:
		sc->sc_busno = result;
		return (0);
	}

	return (ENOENT);
}

static struct resource *
ar71xx_pci_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{

	struct ar71xx_pci_softc *sc = device_get_softc(bus);
	struct resource *rv;
	struct rman *rm;

	switch (type) {
	case SYS_RES_IRQ:
		rm = &sc->sc_irq_rman;
		break;
	case SYS_RES_MEMORY:
		rm = &sc->sc_mem_rman;
		break;
	default:
		return (NULL);
	}

	rv = rman_reserve_resource(rm, start, end, count, flags, child);

	if (rv == NULL)
		return (NULL);

	rman_set_rid(rv, *rid);

	if (flags & RF_ACTIVE) {
		if (bus_activate_resource(child, type, *rid, rv)) {
			rman_release_resource(rv);
			return (NULL);
		}
	}
	return (rv);
}

static int
ar71xx_pci_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	int res = (BUS_ACTIVATE_RESOURCE(device_get_parent(bus),
	    child, type, rid, r));

	if (!res) {
		switch(type) {
		case SYS_RES_MEMORY:
		case SYS_RES_IOPORT:
			rman_set_bustag(r, ar71xx_bus_space_pcimem);
			break;
		}
	}
	return (res);
}

static int
ar71xx_pci_setup_intr(device_t bus, device_t child, struct resource *ires,
	    int flags, driver_filter_t *filt, driver_intr_t *handler,
	    void *arg, void **cookiep)
{
	struct ar71xx_pci_softc *sc = device_get_softc(bus);
	struct intr_event *event;
	int irq, error;

	irq = rman_get_start(ires);

	if (irq > AR71XX_PCI_IRQ_END)
		panic("%s: bad irq %d", __func__, irq);

	event = sc->sc_eventstab[irq];
	if (event == NULL) {
		error = intr_event_create(&event, (void *)irq, 0, irq, 
		    ar71xx_pci_mask_irq, ar71xx_pci_unmask_irq, NULL, NULL,
		    "pci intr%d:", irq);

		if (error == 0) {
			sc->sc_eventstab[irq] = event;
			sc->sc_intr_counter[irq] =
			    mips_intrcnt_create(event->ie_name);
		}
		else
			return (error);
	}

	intr_event_add_handler(event, device_get_nameunit(child), filt,
	    handler, arg, intr_priority(flags), flags, cookiep);
	mips_intrcnt_setname(sc->sc_intr_counter[irq], event->ie_fullname);

	ar71xx_pci_unmask_irq((void*)irq);

	return (0);
}

static int
ar71xx_pci_teardown_intr(device_t dev, device_t child, struct resource *ires,
    void *cookie)
{
	struct ar71xx_pci_softc *sc = device_get_softc(dev);
	int irq, result;

	irq = rman_get_start(ires);
	if (irq > AR71XX_PCI_IRQ_END)
		panic("%s: bad irq %d", __func__, irq);

	if (sc->sc_eventstab[irq] == NULL)
		panic("Trying to teardown unoccupied IRQ");

	ar71xx_pci_mask_irq((void*)irq);

	result = intr_event_remove_handler(cookie);
	if (!result)
		sc->sc_eventstab[irq] = NULL;

	return (result);
}

static int
ar71xx_pci_intr(void *arg)
{
	struct ar71xx_pci_softc *sc = arg;
	struct intr_event *event;
	uint32_t reg, irq, mask;

	reg = ATH_READ_REG(AR71XX_PCI_INTR_STATUS);
	mask = ATH_READ_REG(AR71XX_PCI_INTR_MASK);
	/*
	 * Handle only unmasked interrupts
	 */
	reg &= mask;
	for (irq = AR71XX_PCI_IRQ_START; irq <= AR71XX_PCI_IRQ_END; irq++) {
		if (reg & (1 << irq)) {
			event = sc->sc_eventstab[irq];
			if (!event || CK_SLIST_EMPTY(&event->ie_handlers)) {
				/* Ignore timer interrupts */
				if (irq != 0)
					printf("Stray IRQ %d\n", irq);
				continue;
			}

			/* Flush DDR FIFO for PCI/PCIe */
			ar71xx_device_flush_ddr(AR71XX_CPU_DDR_FLUSH_PCIE);

			/* TODO: frame instead of NULL? */
			intr_event_handle(event, NULL);
			mips_intrcnt_inc(sc->sc_intr_counter[irq]);
		}
	}

	return (FILTER_HANDLED);
}

static int
ar71xx_pci_maxslots(device_t dev)
{

	return (PCI_SLOTMAX);
}

static int
ar71xx_pci_route_interrupt(device_t pcib, device_t device, int pin)
{
	struct ar71xx_pci_softc *sc = device_get_softc(pcib);
	
	if (pci_get_slot(device) < sc->sc_baseslot)
		panic("%s: PCI slot %d is less then AR71XX_PCI_BASE_SLOT",
		    __func__, pci_get_slot(device));

	return (pci_get_slot(device) - sc->sc_baseslot);
}

static device_method_t ar71xx_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ar71xx_pci_probe),
	DEVMETHOD(device_attach,	ar71xx_pci_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	ar71xx_pci_read_ivar),
	DEVMETHOD(bus_write_ivar,	ar71xx_pci_write_ivar),
	DEVMETHOD(bus_alloc_resource,	ar71xx_pci_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, ar71xx_pci_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	ar71xx_pci_setup_intr),
	DEVMETHOD(bus_teardown_intr,	ar71xx_pci_teardown_intr),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	ar71xx_pci_maxslots),
	DEVMETHOD(pcib_read_config,	ar71xx_pci_read_config),
	DEVMETHOD(pcib_write_config,	ar71xx_pci_write_config),
	DEVMETHOD(pcib_route_interrupt,	ar71xx_pci_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	DEVMETHOD_END
};

static driver_t ar71xx_pci_driver = {
	"pcib",
	ar71xx_pci_methods,
	sizeof(struct ar71xx_pci_softc),
};

static devclass_t ar71xx_pci_devclass;

DRIVER_MODULE(ar71xx_pci, nexus, ar71xx_pci_driver, ar71xx_pci_devclass, 0, 0);
