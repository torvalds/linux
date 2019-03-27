/*-
 * Copyright (c) 1999, 2000 Matthew R. Green
 * Copyright (c) 2001 - 2003 by Thomas Moestl <tmm@FreeBSD.org>
 * Copyright (c) 2005 - 2015 by Marius Strobl <marius@FreeBSD.org>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: psycho.c,v 1.35 2001/09/10 16:17:06 eeh Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ofw_pci.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/openfirm.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/asi.h>
#include <machine/bus.h>
#include <machine/bus_private.h>
#include <machine/cpufunc.h>
#include <machine/fsr.h>
#include <machine/resource.h>

#include <sparc64/pci/ofw_pci.h>

int
ofw_pci_attach_common(device_t dev, bus_dma_tag_t dmat, u_long iosize,
    u_long memsize)
{
	struct ofw_pci_softc *sc;
	struct ofw_pci_ranges *range;
	phandle_t node;
	uint32_t prop_array[2];
	u_int i, j, nrange;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);
	sc->sc_node = node;
	sc->sc_pci_dmat = dmat;

	/* Initialize memory and I/O rmans. */
	sc->sc_pci_io_rman.rm_type = RMAN_ARRAY;
	sc->sc_pci_io_rman.rm_descr = "PCI I/O Ports";
	if (rman_init(&sc->sc_pci_io_rman) != 0 ||
	    rman_manage_region(&sc->sc_pci_io_rman, 0, iosize) != 0) {
		device_printf(dev, "failed to set up I/O rman\n");
		return (ENXIO);
	}
	sc->sc_pci_mem_rman.rm_type = RMAN_ARRAY;
	sc->sc_pci_mem_rman.rm_descr = "PCI Memory";
	if (rman_init(&sc->sc_pci_mem_rman) != 0 ||
	    rman_manage_region(&sc->sc_pci_mem_rman, 0, memsize) != 0) {
		device_printf(dev, "failed to set up memory rman\n");
		return (ENXIO);
	}

	/*
	 * Find the addresses of the various bus spaces.  The physical
	 * start addresses of the ranges are the configuration, I/O and
	 * memory handles.  There should not be multiple ones of one kind.
	 */
	nrange = OF_getprop_alloc_multi(node, "ranges", sizeof(*range),
	    (void **)&range);
	for (i = 0; i < nrange; i++) {
		j = OFW_PCI_RANGE_CS(&range[i]);
		if (sc->sc_pci_bh[j] != 0) {
			device_printf(dev, "duplicate range for space %d\n",
			    j);
			OF_prop_free(range);
			return (EINVAL);
		}
		sc->sc_pci_bh[j] = OFW_PCI_RANGE_PHYS(&range[i]);
	}
	OF_prop_free(range);

	/*
	 * Make sure that the expected ranges are actually present.
	 * The OFW_PCI_CS_MEM64 one is not currently used.
	 */
	if (sc->sc_pci_bh[OFW_PCI_CS_CONFIG] == 0) {
		device_printf(dev, "missing CONFIG range\n");
		return (ENXIO);
	}
	if (sc->sc_pci_bh[OFW_PCI_CS_IO] == 0) {
		device_printf(dev, "missing IO range\n");
		return (ENXIO);
	}
	if (sc->sc_pci_bh[OFW_PCI_CS_MEM32] == 0) {
		device_printf(dev, "missing MEM32 range\n");
		return (ENXIO);
	}

	/* Allocate our tags. */
	sc->sc_pci_iot = sparc64_alloc_bus_tag(NULL, PCI_IO_BUS_SPACE);
	if (sc->sc_pci_iot == NULL) {
		device_printf(dev, "could not allocate PCI I/O tag\n");
		return (ENXIO);
	}
	sc->sc_pci_cfgt = sparc64_alloc_bus_tag(NULL, PCI_CONFIG_BUS_SPACE);
	if (sc->sc_pci_cfgt == NULL) {
		device_printf(dev,
		    "could not allocate PCI configuration space tag\n");
		return (ENXIO);
	}

	/*
	 * Get the bus range from the firmware.
	 */
	i = OF_getprop(node, "bus-range", (void *)prop_array,
	    sizeof(prop_array));
	if (i == -1) {
		device_printf(dev, "could not get bus-range\n");
		return (ENXIO);
	}
	if (i != sizeof(prop_array)) {
		device_printf(dev, "broken bus-range (%d)", i);
		return (EINVAL);
	}
	sc->sc_pci_secbus = prop_array[0];
	sc->sc_pci_subbus = prop_array[1];
	if (bootverbose != 0)
		device_printf(dev, "bus range %u to %u; PCI bus %d\n",
		    sc->sc_pci_secbus, sc->sc_pci_subbus, sc->sc_pci_secbus);

	ofw_bus_setup_iinfo(node, &sc->sc_pci_iinfo, sizeof(ofw_pci_intr_t));

	return (0);
}

uint32_t
ofw_pci_read_config_common(device_t dev, u_int regmax, u_long offset,
    u_int bus, u_int slot, u_int func, u_int reg, int width)
{
	struct ofw_pci_softc *sc;
	bus_space_handle_t bh;
	uint32_t r, wrd;
	int i;
	uint16_t shrt;
	uint8_t byte;

	sc = device_get_softc(dev);
	if (bus < sc->sc_pci_secbus || bus > sc->sc_pci_subbus ||
	    slot > PCI_SLOTMAX || func > PCI_FUNCMAX || reg > regmax)
		return (-1);

	bh = sc->sc_pci_bh[OFW_PCI_CS_CONFIG];
	switch (width) {
	case 1:
		i = bus_space_peek_1(sc->sc_pci_cfgt, bh, offset, &byte);
		r = byte;
		break;
	case 2:
		i = bus_space_peek_2(sc->sc_pci_cfgt, bh, offset, &shrt);
		r = shrt;
		break;
	case 4:
		i = bus_space_peek_4(sc->sc_pci_cfgt, bh, offset, &wrd);
		r = wrd;
		break;
	default:
		panic("%s: bad width %d", __func__, width);
		/* NOTREACHED */
	}

	if (i) {
#ifdef OFW_PCI_DEBUG
		printf("%s: read data error reading: %d.%d.%d: 0x%x\n",
		    __func__, bus, slot, func, reg);
#endif
		r = -1;
	}
	return (r);
}

void
ofw_pci_write_config_common(device_t dev, u_int regmax, u_long offset,
    u_int bus, u_int slot, u_int func, u_int reg, uint32_t val, int width)
{
	struct ofw_pci_softc *sc;
	bus_space_handle_t bh;

	sc = device_get_softc(dev);
	if (bus < sc->sc_pci_secbus || bus > sc->sc_pci_subbus ||
	    slot > PCI_SLOTMAX || func > PCI_FUNCMAX || reg > regmax)
		return;

	bh = sc->sc_pci_bh[OFW_PCI_CS_CONFIG];
	switch (width) {
	case 1:
		bus_space_write_1(sc->sc_pci_cfgt, bh, offset, val);
		break;
	case 2:
		bus_space_write_2(sc->sc_pci_cfgt, bh, offset, val);
		break;
	case 4:
		bus_space_write_4(sc->sc_pci_cfgt, bh, offset, val);
		break;
	default:
		panic("%s: bad width %d", __func__, width);
		/* NOTREACHED */
	}
}

ofw_pci_intr_t
ofw_pci_route_interrupt_common(device_t bridge, device_t dev, int pin)
{
	struct ofw_pci_softc *sc;
	struct ofw_pci_register reg;
	ofw_pci_intr_t pintr, mintr;

	sc = device_get_softc(bridge);
	pintr = pin;
	if (ofw_bus_lookup_imap(ofw_bus_get_node(dev), &sc->sc_pci_iinfo,
	    &reg, sizeof(reg), &pintr, sizeof(pintr), &mintr, sizeof(mintr),
	    NULL) != 0)
		return (mintr);
	return (PCI_INVALID_IRQ);
}

void
ofw_pci_dmamap_sync_stst_order_common(void)
{
	static u_char buf[VIS_BLOCKSIZE] __aligned(VIS_BLOCKSIZE);
	register_t reg, s;

	s = intr_disable();
	reg = rd(fprs);
	wr(fprs, reg | FPRS_FEF, 0);
	__asm __volatile("stda %%f0, [%0] %1"
	    : : "r" (buf), "n" (ASI_BLK_COMMIT_S));
	membar(Sync);
	wr(fprs, reg, 0);
	intr_restore(s);
}

int
ofw_pci_read_ivar(device_t dev, device_t child __unused, int which,
    uintptr_t *result)
{
	struct ofw_pci_softc *sc;

	switch (which) {
	case PCIB_IVAR_DOMAIN:
		*result = device_get_unit(dev);
		return (0);
	case PCIB_IVAR_BUS:
		sc = device_get_softc(dev);
		*result = sc->sc_pci_secbus;
		return (0);
	}
	return (ENOENT);
}

struct resource *
ofw_pci_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct ofw_pci_softc *sc;
	struct resource *rv;
	struct rman *rm;

	sc = device_get_softc(bus);
	switch (type) {
	case SYS_RES_IRQ:
		/*
		 * XXX: Don't accept blank ranges for now, only single
		 * interrupts.  The other case should not happen with
		 * the MI PCI code ...
		 * XXX: This may return a resource that is out of the
		 * range that was specified.  Is this correct ...?
		 */
		if (start != end)
			panic("%s: XXX: interrupt range", __func__);
		return (bus_generic_alloc_resource(bus, child, type, rid,
		    start, end, count, flags));
	case SYS_RES_MEMORY:
		rm = &sc->sc_pci_mem_rman;
		break;
	case SYS_RES_IOPORT:
		rm = &sc->sc_pci_io_rman;
		break;
	default:
		return (NULL);
	}

	rv = rman_reserve_resource(rm, start, end, count, flags & ~RF_ACTIVE,
	    child);
	if (rv == NULL)
		return (NULL);
	rman_set_rid(rv, *rid);

	if ((flags & RF_ACTIVE) != 0 && bus_activate_resource(child, type,
	    *rid, rv) != 0) {
		rman_release_resource(rv);
		return (NULL);
	}
	return (rv);
}

int
ofw_pci_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	struct ofw_pci_softc *sc;
	struct bus_space_tag *tag;

	sc = device_get_softc(bus);
	switch (type) {
	case SYS_RES_IRQ:
		return (bus_generic_activate_resource(bus, child, type, rid,
		    r));
	case SYS_RES_MEMORY:
		tag = sparc64_alloc_bus_tag(r, PCI_MEMORY_BUS_SPACE);
		if (tag == NULL)
			return (ENOMEM);
		rman_set_bustag(r, tag);
		rman_set_bushandle(r, sc->sc_pci_bh[OFW_PCI_CS_MEM32] +
		    rman_get_start(r));
		break;
	case SYS_RES_IOPORT:
		rman_set_bustag(r, sc->sc_pci_iot);
		rman_set_bushandle(r, sc->sc_pci_bh[OFW_PCI_CS_IO] +
		    rman_get_start(r));
		break;
	}
	return (rman_activate_resource(r));
}

int
ofw_pci_adjust_resource(device_t bus, device_t child, int type,
    struct resource *r, rman_res_t start, rman_res_t end)
{
	struct ofw_pci_softc *sc;
	struct rman *rm;

	sc = device_get_softc(bus);
	switch (type) {
	case SYS_RES_IRQ:
		return (bus_generic_adjust_resource(bus, child, type, r,
		    start, end));
	case SYS_RES_MEMORY:
		rm = &sc->sc_pci_mem_rman;
		break;
	case SYS_RES_IOPORT:
		rm = &sc->sc_pci_io_rman;
		break;
	default:
		return (EINVAL);
	}
	if (rman_is_region_manager(r, rm) == 0)
		return (EINVAL);
	return (rman_adjust_resource(r, start, end));
}

bus_dma_tag_t
ofw_pci_get_dma_tag(device_t bus, device_t child __unused)
{
	struct ofw_pci_softc *sc;

	sc = device_get_softc(bus);
	return (sc->sc_pci_dmat);
}

phandle_t
ofw_pci_get_node(device_t bus, device_t child __unused)
{
	struct ofw_pci_softc *sc;

	sc = device_get_softc(bus);
	/* We only have one child, the PCI bus, which needs our own node. */
	return (sc->sc_node);
}
