/*
 * Copyright (c) 2017 Semihalf.
 * Copyright (c) 2017 Stormshield.
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

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/rman.h>
#include <sys/unistd.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/ahci/ahci.h>

#define		AHCI_VENDOR_SPECIFIC_0_ADDR	0xa0
#define		AHCI_VENDOR_SPECIFIC_0_DATA	0xa4

#define		AHCI_HC_DEVSTR		"Marvell AHCI Controller"
#define		AHCI_HC_VENDOR		"Marvell"

static device_attach_t ahci_mv_fdt_attach;

static struct ofw_compat_data compatible_data[] = {
	{"marvell,armada-380-ahci",	true},
	{NULL,				false}
};

static void
ahci_mv_regret_config(struct ahci_controller *ctlr)
{

	/*
	 * Enable the regret bit to allow the SATA unit to regret
	 * a request that didn't receive an acknowledge
	 * and a avoid deadlock
	 */
	ATA_OUTL(ctlr->r_mem, AHCI_VENDOR_SPECIFIC_0_ADDR, 0x4);
	ATA_OUTL(ctlr->r_mem, AHCI_VENDOR_SPECIFIC_0_DATA, 0x80);
}

static int
ahci_mv_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compatible_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, AHCI_HC_DEVSTR);

	return (BUS_PROBE_DEFAULT);
}

static int
ahci_mv_fdt_attach(device_t dev)
{
	struct ahci_controller *ctlr;
	int rc;

	ctlr = device_get_softc(dev);
	ctlr->dev = dev;
	ctlr->r_rid = 0;
	ctlr->quirks = AHCI_Q_2CH;
	ctlr->numirqs = 1;

	if (ofw_bus_is_compatible(dev, "marvell,armada-380-ahci"))
		ctlr->quirks |= AHCI_Q_MRVL_SR_DEL;

	/* Allocate memory for controller */
	ctlr->r_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &ctlr->r_rid, RF_ACTIVE | RF_SHAREABLE);
	if (ctlr->r_mem == NULL) {
		device_printf(dev, "Failed to alloc memory for controller\n");
		return (ENOMEM);
	}

	/* Reset controller */
	rc = ahci_ctlr_reset(dev);
	if (rc != 0) {
		device_printf(dev, "Failed to reset controller\n");
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
		return (ENXIO);
	}

	ahci_mv_regret_config(ctlr);

	rc = ahci_attach(dev);
	if (rc != 0) {
		device_printf(dev, "Failed to initialize AHCI, with error %d\n", rc);
		return (ENXIO);
	}

	return (0);
}

static device_method_t ahci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			ahci_mv_fdt_probe),
	DEVMETHOD(device_attach,		ahci_mv_fdt_attach),
	DEVMETHOD(device_detach,		ahci_detach),
	DEVMETHOD(bus_alloc_resource,       	ahci_alloc_resource),
	DEVMETHOD(bus_release_resource,     	ahci_release_resource),
	DEVMETHOD(bus_setup_intr,   		ahci_setup_intr),
	DEVMETHOD(bus_teardown_intr,		ahci_teardown_intr),
	DEVMETHOD(bus_print_child,		ahci_print_child),
	DEVMETHOD(bus_child_location_str, 	ahci_child_location_str),
	DEVMETHOD(bus_get_dma_tag,  		ahci_get_dma_tag),
	DEVMETHOD_END
};

static driver_t ahci_driver = {
	"ahci",
	ahci_methods,
	sizeof(struct ahci_controller)
};

DRIVER_MODULE(ahci_mv, simplebus, ahci_driver, ahci_devclass, NULL, NULL);
DRIVER_MODULE(ahci_mv, ofwbus, ahci_driver, ahci_devclass, NULL, NULL);
