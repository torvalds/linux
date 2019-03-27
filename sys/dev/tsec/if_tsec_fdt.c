/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2007-2008 Semihalf, Rafal Jaworowski
 * Copyright (C) 2006-2007 Semihalf, Piotr Kruszynski
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * From: FreeBSD: head/sys/dev/tsec/if_tsec_ocp.c 188712 2009-02-17 14:59:47Z raj
 */

/*
 * FDT 'simple-bus' attachment for Freescale TSEC controller.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>

#include <dev/fdt/fdt_common.h>
#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <dev/tsec/if_tsec.h>
#include <dev/tsec/if_tsecreg.h>

#include "miibus_if.h"

#define	TSEC_RID_TXIRQ	0
#define	TSEC_RID_RXIRQ	1
#define	TSEC_RID_ERRIRQ	2

static int tsec_fdt_probe(device_t dev);
static int tsec_fdt_attach(device_t dev);
static int tsec_fdt_detach(device_t dev);
static int tsec_setup_intr(struct tsec_softc *sc, struct resource **ires,
    void **ihand, int *irid, driver_intr_t handler, const char *iname);
static void tsec_release_intr(struct tsec_softc *sc, struct resource *ires,
    void *ihand, int irid, const char *iname);

static device_method_t tsec_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tsec_fdt_probe),
	DEVMETHOD(device_attach,	tsec_fdt_attach),
	DEVMETHOD(device_detach,	tsec_fdt_detach),

	DEVMETHOD(device_shutdown,	tsec_shutdown),
	DEVMETHOD(device_suspend,	tsec_suspend),
	DEVMETHOD(device_resume,	tsec_resume),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	tsec_miibus_readreg),
	DEVMETHOD(miibus_writereg,	tsec_miibus_writereg),
	DEVMETHOD(miibus_statchg,	tsec_miibus_statchg),

	DEVMETHOD_END
};

static driver_t tsec_fdt_driver = {
	"tsec",
	tsec_methods,
	sizeof(struct tsec_softc),
};

DRIVER_MODULE(tsec, simplebus, tsec_fdt_driver, tsec_devclass, 0, 0);

static int
tsec_fdt_probe(device_t dev)
{
	struct tsec_softc *sc;
	uint32_t id;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_get_type(dev) == NULL ||
	    strcmp(ofw_bus_get_type(dev), "network") != 0)
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "gianfar") &&
	    !ofw_bus_is_compatible(dev, "fsl,etsec2"))
		return (ENXIO);

	sc = device_get_softc(dev);

	/*
	 * Device trees with "fsl,etsec2" compatible nodes don't have a reg
	 * property, as it's been relegated to the queue-group children.
	 */
	if (ofw_bus_is_compatible(dev, "fsl,etsec2"))
		sc->is_etsec = 1;
	else {
		sc->sc_rrid = 0;
		sc->sc_rres = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->sc_rrid,
		    RF_ACTIVE);
		if (sc->sc_rres == NULL)
			return (ENXIO);

		sc->sc_bas.bsh = rman_get_bushandle(sc->sc_rres);
		sc->sc_bas.bst = rman_get_bustag(sc->sc_rres);

		/* Check if we are eTSEC (enhanced TSEC) */
		id = TSEC_READ(sc, TSEC_REG_ID);
		sc->is_etsec = ((id >> 16) == TSEC_ETSEC_ID) ? 1 : 0;
		id |= TSEC_READ(sc, TSEC_REG_ID2);

		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_rrid, sc->sc_rres);

		if (id == 0) {
			device_printf(dev, "could not identify TSEC type\n");
			return (ENXIO);
		}
	}

	if (sc->is_etsec)
		device_set_desc(dev, "Enhanced Three-Speed Ethernet Controller");
	else
		device_set_desc(dev, "Three-Speed Ethernet Controller");

	return (BUS_PROBE_DEFAULT);
}

static int
tsec_fdt_attach(device_t dev)
{
	struct tsec_softc *sc;
	struct resource_list *rl;
	phandle_t child, mdio, phy;
	int acells, scells;
	int error = 0;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->node = ofw_bus_get_node(dev);

	if (fdt_addrsize_cells(sc->node, &acells, &scells) != 0) {
		acells = 1;
		scells = 1;
	}
	if (ofw_bus_is_compatible(dev, "fsl,etsec2")) {
		rl = BUS_GET_RESOURCE_LIST(device_get_parent(dev), dev);

		/*
		 * TODO: Add all children resources to the list.  Will be
		 * required to support multigroup mode.
		 */
		child = OF_child(sc->node);
		ofw_bus_reg_to_rl(dev, child, acells, scells, rl);
		ofw_bus_intr_to_rl(dev, child, rl, NULL);
	}

	/* Get phy address from fdt */
	if (OF_getencprop(sc->node, "phy-handle", &phy, sizeof(phy)) <= 0) {
		device_printf(dev, "PHY not found in device tree");
		return (ENXIO);
	}

	phy = OF_node_from_xref(phy);
	mdio = OF_parent(phy);
	OF_decode_addr(mdio, 0, &sc->phy_bst, &sc->phy_bsh, NULL);
	OF_getencprop(phy, "reg", &sc->phyaddr, sizeof(sc->phyaddr));

	/*
	 * etsec2 MDIO nodes are given the MDIO module base address, so we need
	 * to add the MII offset to get the PHY registers.
	 */
	if (ofw_bus_node_is_compatible(mdio, "fsl,etsec2-mdio"))
		sc->phy_regoff = TSEC_REG_MIIBASE;

	/* Init timer */
	callout_init(&sc->tsec_callout, 1);

	/* Init locks */
	mtx_init(&sc->transmit_lock, device_get_nameunit(dev), "TSEC TX lock",
	    MTX_DEF);
	mtx_init(&sc->receive_lock, device_get_nameunit(dev), "TSEC RX lock",
	    MTX_DEF);
	mtx_init(&sc->ic_lock, device_get_nameunit(dev), "TSEC IC lock",
	    MTX_DEF);

	/* Allocate IO memory for TSEC registers */
	sc->sc_rrid = 0;
	sc->sc_rres = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->sc_rrid,
	    RF_ACTIVE);
	if (sc->sc_rres == NULL) {
		device_printf(dev, "could not allocate IO memory range!\n");
		goto fail1;
	}
	sc->sc_bas.bsh = rman_get_bushandle(sc->sc_rres);
	sc->sc_bas.bst = rman_get_bustag(sc->sc_rres);

	/* TSEC attach */
	if (tsec_attach(sc) != 0) {
		device_printf(dev, "could not be configured\n");
		goto fail2;
	}

	/* Set up interrupts (TX/RX/ERR) */
	sc->sc_transmit_irid = TSEC_RID_TXIRQ;
	error = tsec_setup_intr(sc, &sc->sc_transmit_ires,
	    &sc->sc_transmit_ihand, &sc->sc_transmit_irid,
	    tsec_transmit_intr, "TX");
	if (error)
		goto fail2;

	sc->sc_receive_irid = TSEC_RID_RXIRQ;
	error = tsec_setup_intr(sc, &sc->sc_receive_ires,
	    &sc->sc_receive_ihand, &sc->sc_receive_irid,
	    tsec_receive_intr, "RX");
	if (error)
		goto fail3;

	sc->sc_error_irid = TSEC_RID_ERRIRQ;
	error = tsec_setup_intr(sc, &sc->sc_error_ires,
	    &sc->sc_error_ihand, &sc->sc_error_irid,
	    tsec_error_intr, "ERR");
	if (error)
		goto fail4;

	return (0);

fail4:
	tsec_release_intr(sc, sc->sc_receive_ires, sc->sc_receive_ihand,
	    sc->sc_receive_irid, "RX");
fail3:
	tsec_release_intr(sc, sc->sc_transmit_ires, sc->sc_transmit_ihand,
	    sc->sc_transmit_irid, "TX");
fail2:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_rrid, sc->sc_rres);
fail1:
	mtx_destroy(&sc->receive_lock);
	mtx_destroy(&sc->transmit_lock);
	return (ENXIO);
}

static int
tsec_setup_intr(struct tsec_softc *sc, struct resource **ires, void **ihand,
    int *irid, driver_intr_t handler, const char *iname)
{
	int error;

	*ires = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, irid, RF_ACTIVE);
	if (*ires == NULL) {
		device_printf(sc->dev, "could not allocate %s IRQ\n", iname);
		return (ENXIO);
	}
	error = bus_setup_intr(sc->dev, *ires, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, handler, sc, ihand);
	if (error) {
		device_printf(sc->dev, "failed to set up %s IRQ\n", iname);
		if (bus_release_resource(sc->dev, SYS_RES_IRQ, *irid, *ires))
			device_printf(sc->dev, "could not release %s IRQ\n", iname);
		*ires = NULL;
		return (error);
	}
	return (0);
}

static void
tsec_release_intr(struct tsec_softc *sc, struct resource *ires, void *ihand,
    int irid, const char *iname)
{
	int error;

	if (ires == NULL)
		return;

	error = bus_teardown_intr(sc->dev, ires, ihand);
	if (error)
		device_printf(sc->dev, "bus_teardown_intr() failed for %s intr"
		    ", error %d\n", iname, error);

	error = bus_release_resource(sc->dev, SYS_RES_IRQ, irid, ires);
	if (error)
		device_printf(sc->dev, "bus_release_resource() failed for %s "
		    "intr, error %d\n", iname, error);
}

static int
tsec_fdt_detach(device_t dev)
{
	struct tsec_softc *sc;
	int error;

	sc = device_get_softc(dev);

	/* Wait for stopping watchdog */
	callout_drain(&sc->tsec_callout);

	/* Stop and release all interrupts */
	tsec_release_intr(sc, sc->sc_transmit_ires, sc->sc_transmit_ihand,
	    sc->sc_transmit_irid, "TX");
	tsec_release_intr(sc, sc->sc_receive_ires, sc->sc_receive_ihand,
	    sc->sc_receive_irid, "RX");
	tsec_release_intr(sc, sc->sc_error_ires, sc->sc_error_ihand,
	    sc->sc_error_irid, "ERR");

	/* TSEC detach */
	tsec_detach(sc);

	/* Free IO memory handler */
	if (sc->sc_rres) {
		error = bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_rrid,
		    sc->sc_rres);
		if (error)
			device_printf(dev, "bus_release_resource() failed for"
			    " IO memory, error %d\n", error);
	}

	/* Destroy locks */
	mtx_destroy(&sc->receive_lock);
	mtx_destroy(&sc->transmit_lock);
	mtx_destroy(&sc->ic_lock);
	return (0);
}

void
tsec_get_hwaddr(struct tsec_softc *sc, uint8_t *addr)
{
	union {
		uint32_t reg[2];
		uint8_t addr[6];
	} hw;
	int i;

	hw.reg[0] = hw.reg[1] = 0;

	/* Retrieve the hardware address from the device tree. */
	i = OF_getprop(sc->node, "local-mac-address", (void *)hw.addr, 6);
	if (i == 6 && (hw.reg[0] != 0 || hw.reg[1] != 0)) {
		bcopy(hw.addr, addr, 6);
		return;
	}

	/* Also try the mac-address property, which is second-best */
	i = OF_getprop(sc->node, "mac-address", (void *)hw.addr, 6);
	if (i == 6 && (hw.reg[0] != 0 || hw.reg[1] != 0)) {
		bcopy(hw.addr, addr, 6);
		return;
	}

	/*
	 * Fall back -- use the currently programmed address in the hope that
	 * it was set be firmware...
	 */
	hw.reg[0] = TSEC_READ(sc, TSEC_REG_MACSTNADDR1);
	hw.reg[1] = TSEC_READ(sc, TSEC_REG_MACSTNADDR2);
	for (i = 0; i < 6; i++)
		addr[5-i] = hw.addr[i];
}
