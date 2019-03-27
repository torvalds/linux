/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * Copyright (c) 2011, Luiz Otavio O Souza.
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
#include <mips/atheros/ar724xreg.h>
#include <mips/atheros/ar71xx_setup.h>
#include <mips/atheros/ar71xx_pci_bus_space.h>

#include <mips/atheros/ar71xx_cpudef.h>

#ifdef	AR71XX_ATH_EEPROM
#include <mips/atheros/ar71xx_fixup.h>
#endif	/* AR71XX_ATH_EEPROM */

#undef	AR724X_PCI_DEBUG
#ifdef AR724X_PCI_DEBUG
#define dprintf printf
#else
#define dprintf(x, arg...)
#endif

struct ar71xx_pci_softc {
	device_t		sc_dev;

	int			sc_busno;
	struct rman		sc_mem_rman;
	struct rman		sc_irq_rman;

	struct intr_event	*sc_eventstab[AR71XX_PCI_NIRQS];	
	mips_intrcnt_t		sc_intr_counter[AR71XX_PCI_NIRQS];	
	struct resource		*sc_irq;
	void			*sc_ih;
};

static int ar724x_pci_setup_intr(device_t, device_t, struct resource *, int, 
		    driver_filter_t *, driver_intr_t *, void *, void **);
static int ar724x_pci_teardown_intr(device_t, device_t, struct resource *,
		    void *);
static int ar724x_pci_intr(void *);

static void
ar724x_pci_write(uint32_t reg, uint32_t offset, uint32_t data, int bytes)
{
	uint32_t val, mask, shift;

	/* Register access is 32-bit aligned */
	shift = (offset & 3) * 8;
	if (bytes % 4)
		mask = (1 << (bytes * 8)) - 1;
	else
		mask = 0xffffffff;

	rmb();
	val = ATH_READ_REG(reg + (offset & ~3));
	val &= ~(mask << shift);
	val |= ((data & mask) << shift);
	ATH_WRITE_REG(reg + (offset & ~3), val);
	wmb();

	dprintf("%s: %#x/%#x addr=%#x, data=%#x(%#x), bytes=%d\n", __func__, 
	    reg, reg + (offset & ~3), offset, data, val, bytes);
}

static uint32_t
ar724x_pci_read_config(device_t dev, u_int bus, u_int slot, u_int func, 
    u_int reg, int bytes)
{
	uint32_t data, shift, mask;

	/* Register access is 32-bit aligned */
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

	rmb();
	if ((bus == 0) && (slot == 0) && (func == 0))
		data = ATH_READ_REG(AR724X_PCI_CFG_BASE + (reg & ~3));
	else
		data = -1;

	/* Get request bytes from 32-bit word */
	data = (data >> shift) & mask;

	dprintf("%s: read 0x%x\n", __func__, data);

	return (data);
}

static void
ar724x_pci_write_config(device_t dev, u_int bus, u_int slot, u_int func, 
    u_int reg, uint32_t data, int bytes)
{

	dprintf("%s: tag (%x, %x, %x) reg %d(%d): %x\n", __func__, bus, slot, 
	    func, reg, bytes, data);

	if ((bus != 0) || (slot != 0) || (func != 0))
		return;

	/*
	 * WAR for BAR issue on AR7240 - We are unable to access the PCI
	 * device space if we set the BAR with proper base address.
	 *
	 * However, we _do_ want to allow programming in the probe value
	 * (0xffffffff) so the PCI code can find out how big the memory
	 * map is for this device.  Without it, it'll think the memory
	 * map is 32 bits wide, the PCI code will then end up thinking
	 * the register window is '0' and fail to allocate resources.
	 *
	 * Note: Test on AR7241/AR7242/AR9344! Those use a WAR value of
	 * 0x1000ffff.
	 */
	if (reg == PCIR_BAR(0) && bytes == 4
	    && ar71xx_soc == AR71XX_SOC_AR7240
	    && data != 0xffffffff)
		ar724x_pci_write(AR724X_PCI_CFG_BASE, reg, 0xffff, bytes);
	else
		ar724x_pci_write(AR724X_PCI_CFG_BASE, reg, data, bytes);
}

static void 
ar724x_pci_mask_irq(void *source)
{
	uint32_t reg;
	unsigned int irq = (unsigned int)source;

	/* XXX - Only one interrupt ? Only one device ? */
	if (irq != AR71XX_PCI_IRQ_START)
		return;

	/* Update the interrupt mask reg */
	reg = ATH_READ_REG(AR724X_PCI_INTR_MASK);
	ATH_WRITE_REG(AR724X_PCI_INTR_MASK,
	    reg & ~AR724X_PCI_INTR_DEV0);

	/* Clear any pending interrupt */
	reg = ATH_READ_REG(AR724X_PCI_INTR_STATUS);
	ATH_WRITE_REG(AR724X_PCI_INTR_STATUS,
	    reg | AR724X_PCI_INTR_DEV0);
}

static void 
ar724x_pci_unmask_irq(void *source)
{
	uint32_t reg;
	unsigned int irq = (unsigned int)source;

	/* XXX */
	if (irq != AR71XX_PCI_IRQ_START)
		return;

	/* Update the interrupt mask reg */
	reg = ATH_READ_REG(AR724X_PCI_INTR_MASK);
	ATH_WRITE_REG(AR724X_PCI_INTR_MASK,
	    reg | AR724X_PCI_INTR_DEV0);
}

static int
ar724x_pci_setup(device_t dev)
{
	uint32_t reg;

	/* setup COMMAND register */
	reg = PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN | PCIM_CMD_SERRESPEN |
	    PCIM_CMD_BACKTOBACK | PCIM_CMD_PERRESPEN | PCIM_CMD_MWRICEN;

	ar724x_pci_write(AR724X_PCI_CRP_BASE, PCIR_COMMAND, reg, 2);
	ar724x_pci_write(AR724X_PCI_CRP_BASE, 0x20, 0x1ff01000, 4);
	ar724x_pci_write(AR724X_PCI_CRP_BASE, 0x24, 0x1ff01000, 4);

	reg = ATH_READ_REG(AR724X_PCI_RESET);
	if (reg != 0x7) {
		DELAY(100000);
		ATH_WRITE_REG(AR724X_PCI_RESET, 0);
		DELAY(100);
		ATH_WRITE_REG(AR724X_PCI_RESET, 4);
		DELAY(100000);
	}

	if (ar71xx_soc == AR71XX_SOC_AR7240)
		reg = AR724X_PCI_APP_LTSSM_ENABLE;
	else
		reg = 0x1ffc1;
	ATH_WRITE_REG(AR724X_PCI_APP, reg);
	/* Flush write */
	(void) ATH_READ_REG(AR724X_PCI_APP);

	DELAY(1000);

	reg = ATH_READ_REG(AR724X_PCI_RESET);
	if ((reg & AR724X_PCI_RESET_LINK_UP) == 0) {
		device_printf(dev, "no PCIe controller found\n");
		return (ENXIO);
	}

	if (ar71xx_soc == AR71XX_SOC_AR7241 ||
	    ar71xx_soc == AR71XX_SOC_AR7242) {
		reg = ATH_READ_REG(AR724X_PCI_APP);
		reg |= (1 << 16);
		ATH_WRITE_REG(AR724X_PCI_APP, reg);
	}

	return (0);
}

#ifdef	AR71XX_ATH_EEPROM
#define	AR5416_EEPROM_MAGIC		0xa55a

/*
 * XXX - This should not be here ! And this looks like Atheros (if_ath) only.
 */
static void
ar724x_pci_fixup(device_t dev, long flash_addr, int len)
{
	uint32_t bar0, reg, val;
	uint16_t *cal_data = (uint16_t *) MIPS_PHYS_TO_KSEG1(flash_addr);

#if 0
	if (cal_data[0] != AR5416_EEPROM_MAGIC) {
		device_printf(dev, "%s: Invalid calibration data from 0x%x\n",
		    __func__, (uintptr_t) flash_addr);
		return;
	}
#endif

	/* Save bar(0) address - just to flush bar(0) (SoC WAR) ? */
	bar0 = ar724x_pci_read_config(dev, 0, 0, 0, PCIR_BAR(0), 4);

	/* Write temporary BAR0 to map the NIC into a fixed location */
	/* XXX AR7240: 0xffff; 7241/7242/9344: 0x1000ffff */
	ar724x_pci_write_config(dev, 0, 0, 0, PCIR_BAR(0),
	    AR71XX_PCI_MEM_BASE, 4);

	val = ar724x_pci_read_config(dev, 0, 0, 0, PCIR_COMMAND, 2);
	val |= (PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN);
	ar724x_pci_write_config(dev, 0, 0, 0, PCIR_COMMAND, val, 2); 

	/* set pointer to first reg address */
	cal_data += 3;
	while (*cal_data != 0xffff) {
		reg = *cal_data++;
		val = *cal_data++;
		val |= (*cal_data++) << 16;

		if (bootverbose)
			printf("    0x%08x=0x%08x\n", reg, val);

		/* Write eeprom fixup data to device memory */
		ATH_WRITE_REG(AR71XX_PCI_MEM_BASE + reg, val);
		DELAY(100);
	}

	val = ar724x_pci_read_config(dev, 0, 0, 0, PCIR_COMMAND, 2);
	val &= ~(PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN);
	ar724x_pci_write_config(dev, 0, 0, 0, PCIR_COMMAND, val, 2); 

	/* Write the saved bar(0) address */
	ar724x_pci_write_config(dev, 0, 0, 0, PCIR_BAR(0), bar0, 4);
}
#undef	AR5416_EEPROM_MAGIC

/*
 * XXX This is (mostly) duplicated with ar71xx_pci.c.
 * It should at some point be fixed.
 */
static void
ar724x_pci_slot_fixup(device_t dev)
{
	long int flash_addr;
	char buf[64];
	int size;

	/*
	 * Check whether the given slot has a hint to poke.
	 */
	if (bootverbose)
	device_printf(dev, "%s: checking dev %s, %d/%d/%d\n",
	    __func__, device_get_nameunit(dev), 0, 0, 0);

	snprintf(buf, sizeof(buf), "bus.%d.%d.%d.ath_fixup_addr",
	    0, 0, 0);

	if (resource_long_value(device_get_name(dev), device_get_unit(dev),
	    buf, &flash_addr) == 0) {
		snprintf(buf, sizeof(buf), "bus.%d.%d.%d.ath_fixup_size",
		    0, 0, 0);
		if (resource_int_value(device_get_name(dev),
		    device_get_unit(dev), buf, &size) != 0) {
			device_printf(dev,
			    "%s: missing hint '%s', aborting EEPROM\n",
			    __func__, buf);
			return;
		}

		device_printf(dev, "found EEPROM at 0x%lx on %d.%d.%d\n",
		    flash_addr, 0, 0, 0);
		ar724x_pci_fixup(dev, flash_addr, size);
		ar71xx_pci_slot_create_eeprom_firmware(dev, 0, 0, 0,
		    flash_addr, size);
	}
}
#endif	/* AR71XX_ATH_EEPROM */

static int
ar724x_pci_probe(device_t dev)
{

	return (BUS_PROBE_NOWILDCARD);
}

static int
ar724x_pci_attach(device_t dev)
{
	struct ar71xx_pci_softc *sc = device_get_softc(dev);
	int rid = 0;

	sc->sc_mem_rman.rm_type = RMAN_ARRAY;
	sc->sc_mem_rman.rm_descr = "ar724x PCI memory window";
	if (rman_init(&sc->sc_mem_rman) != 0 || 
	    rman_manage_region(&sc->sc_mem_rman, AR71XX_PCI_MEM_BASE, 
		AR71XX_PCI_MEM_BASE + AR71XX_PCI_MEM_SIZE - 1) != 0) {
		panic("ar724x_pci_attach: failed to set up I/O rman");
	}

	sc->sc_irq_rman.rm_type = RMAN_ARRAY;
	sc->sc_irq_rman.rm_descr = "ar724x PCI IRQs";
	if (rman_init(&sc->sc_irq_rman) != 0 ||
	    rman_manage_region(&sc->sc_irq_rman, AR71XX_PCI_IRQ_START, 
	        AR71XX_PCI_IRQ_END) != 0)
		panic("ar724x_pci_attach: failed to set up IRQ rman");

	/* Disable interrupts */
	ATH_WRITE_REG(AR724X_PCI_INTR_STATUS, 0);
	ATH_WRITE_REG(AR724X_PCI_INTR_MASK, 0);

	/* Hook up our interrupt handler. */
	if ((sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE)) == NULL) {
		device_printf(dev, "unable to allocate IRQ resource\n");
		return (ENXIO);
	}

	if ((bus_setup_intr(dev, sc->sc_irq, INTR_TYPE_MISC,
			    ar724x_pci_intr, NULL, sc, &sc->sc_ih))) {
		device_printf(dev, 
		    "WARNING: unable to register interrupt handler\n");
		return (ENXIO);
	}

	/* Reset PCIe core and PCIe PHY */
	ar71xx_device_stop(AR724X_RESET_PCIE);
	ar71xx_device_stop(AR724X_RESET_PCIE_PHY);
	ar71xx_device_stop(AR724X_RESET_PCIE_PHY_SERIAL);
	DELAY(100);

	ar71xx_device_start(AR724X_RESET_PCIE_PHY_SERIAL);
	DELAY(100);
	ar71xx_device_start(AR724X_RESET_PCIE_PHY);
	ar71xx_device_start(AR724X_RESET_PCIE);

	if (ar724x_pci_setup(dev))
		return (ENXIO);

#ifdef	AR71XX_ATH_EEPROM
	ar724x_pci_slot_fixup(dev);
#endif	/* AR71XX_ATH_EEPROM */

	/* Fixup internal PCI bridge */
	ar724x_pci_write_config(dev, 0, 0, 0, PCIR_COMMAND, 
            PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN 
	    | PCIM_CMD_SERRESPEN | PCIM_CMD_BACKTOBACK
	    | PCIM_CMD_PERRESPEN | PCIM_CMD_MWRICEN, 2);

	device_add_child(dev, "pci", -1);
	return (bus_generic_attach(dev));
}

static int
ar724x_pci_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
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
ar724x_pci_write_ivar(device_t dev, device_t child, int which, uintptr_t result)
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
ar724x_pci_alloc_resource(device_t bus, device_t child, int type, int *rid,
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
ar724x_pci_activate_resource(device_t bus, device_t child, int type, int rid,
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
ar724x_pci_setup_intr(device_t bus, device_t child, struct resource *ires,
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
		    ar724x_pci_mask_irq, ar724x_pci_unmask_irq, NULL, NULL,
		    "pci intr%d:", irq);

		if (error == 0) {
			sc->sc_eventstab[irq] = event;
			sc->sc_intr_counter[irq] =
			    mips_intrcnt_create(event->ie_name);
		}
		else
			return error;
	}

	intr_event_add_handler(event, device_get_nameunit(child), filt,
	    handler, arg, intr_priority(flags), flags, cookiep);
	mips_intrcnt_setname(sc->sc_intr_counter[irq], event->ie_fullname);

	ar724x_pci_unmask_irq((void*)irq);

	return (0);
}

static int
ar724x_pci_teardown_intr(device_t dev, device_t child, struct resource *ires,
    void *cookie)
{
	struct ar71xx_pci_softc *sc = device_get_softc(dev);
	int irq, result;

	irq = rman_get_start(ires);
	if (irq > AR71XX_PCI_IRQ_END)
		panic("%s: bad irq %d", __func__, irq);

	if (sc->sc_eventstab[irq] == NULL)
		panic("Trying to teardown unoccupied IRQ");

	ar724x_pci_mask_irq((void*)irq);

	result = intr_event_remove_handler(cookie);
	if (!result)
		sc->sc_eventstab[irq] = NULL;

	return (result);
}

static int
ar724x_pci_intr(void *arg)
{
	struct ar71xx_pci_softc *sc = arg;
	struct intr_event *event;
	uint32_t reg, irq, mask;


	reg = ATH_READ_REG(AR724X_PCI_INTR_STATUS);
	mask = ATH_READ_REG(AR724X_PCI_INTR_MASK);
	/*
	 * Handle only unmasked interrupts
	 */
	reg &= mask;
	if (reg & AR724X_PCI_INTR_DEV0) {

		irq = AR71XX_PCI_IRQ_START;
		event = sc->sc_eventstab[irq];
		if (!event || CK_SLIST_EMPTY(&event->ie_handlers)) {
			printf("Stray IRQ %d\n", irq);
			return (FILTER_STRAY);
		}

		/* Flush pending memory transactions */
		ar71xx_device_flush_ddr(AR71XX_CPU_DDR_FLUSH_PCIE);

		/* TODO: frame instead of NULL? */
		intr_event_handle(event, NULL);
		mips_intrcnt_inc(sc->sc_intr_counter[irq]);
	}

	return (FILTER_HANDLED);
}

static int
ar724x_pci_maxslots(device_t dev)
{

	return (PCI_SLOTMAX);
}

static int
ar724x_pci_route_interrupt(device_t pcib, device_t device, int pin)
{

	return (pci_get_slot(device));
}

static device_method_t ar724x_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ar724x_pci_probe),
	DEVMETHOD(device_attach,	ar724x_pci_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	ar724x_pci_read_ivar),
	DEVMETHOD(bus_write_ivar,	ar724x_pci_write_ivar),
	DEVMETHOD(bus_alloc_resource,	ar724x_pci_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, ar724x_pci_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	ar724x_pci_setup_intr),
	DEVMETHOD(bus_teardown_intr,	ar724x_pci_teardown_intr),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	ar724x_pci_maxslots),
	DEVMETHOD(pcib_read_config,	ar724x_pci_read_config),
	DEVMETHOD(pcib_write_config,	ar724x_pci_write_config),
	DEVMETHOD(pcib_route_interrupt,	ar724x_pci_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	DEVMETHOD_END
};

static driver_t ar724x_pci_driver = {
	"pcib",
	ar724x_pci_methods,
	sizeof(struct ar71xx_pci_softc),
};

static devclass_t ar724x_pci_devclass;

DRIVER_MODULE(ar724x_pci, nexus, ar724x_pci_driver, ar724x_pci_devclass, 0, 0);
