/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994,1995 Stefan Esser, Wolfgang StanglMeier
 * Copyright (c) 2000 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2001, 2003 Thomas Moestl <tmm@FreeBSD.org>
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
 *
 *	from: FreeBSD: src/sys/dev/pci/pci_pci.c,v 1.3 2000/12/13
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Support for the Sun APB (Advanced PCI Bridge) PCI-PCI bridge.
 * This bridge does not fully comply to the PCI bridge specification, and is
 * therefore not supported by the generic driver.
 * We can use some of the pcib methods anyway.
 */

#include "opt_ofw_pci.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcib_private.h>

#include "pcib_if.h"

#include <sparc64/pci/ofw_pci.h>
#include <sparc64/pci/ofw_pcib_subr.h>

/*
 * Bridge-specific data.
 */
struct apb_softc {
	struct ofw_pcib_gen_softc	sc_bsc;
	uint8_t		sc_iomap;
	uint8_t		sc_memmap;
};

static device_probe_t apb_probe;
static device_attach_t apb_attach;
static bus_alloc_resource_t apb_alloc_resource;
static bus_adjust_resource_t apb_adjust_resource;

static device_method_t apb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		apb_probe),
	DEVMETHOD(device_attach,	apb_attach),

	/* Bus interface */
	DEVMETHOD(bus_alloc_resource,	apb_alloc_resource),
	DEVMETHOD(bus_adjust_resource,	apb_adjust_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),

	/* pcib interface */
	DEVMETHOD(pcib_route_interrupt,	ofw_pcib_gen_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	ofw_pcib_gen_get_node),

	DEVMETHOD_END
};

static devclass_t pcib_devclass;

DEFINE_CLASS_1(pcib, apb_driver, apb_methods, sizeof(struct apb_softc),
    pcib_driver);
EARLY_DRIVER_MODULE(apb, pci, apb_driver, pcib_devclass, 0, 0, BUS_PASS_BUS);
MODULE_DEPEND(apb, pci, 1, 1, 1);

/* APB specific registers */
#define	APBR_IOMAP	0xde
#define	APBR_MEMMAP	0xdf

/* Definitions for the mapping registers */
#define	APB_IO_SCALE	0x200000
#define	APB_MEM_SCALE	0x20000000

/*
 * Generic device interface
 */
static int
apb_probe(device_t dev)
{

	if (pci_get_vendor(dev) == 0x108e &&	/* Sun */
	    pci_get_device(dev) == 0x5000)  {	/* APB */
		device_set_desc(dev, "APB PCI-PCI bridge");
		return (0);
	}
	return (ENXIO);
}

static void
apb_map_print(uint8_t map, rman_res_t scale)
{
	int i, first;

	for (first = 1, i = 0; i < 8; i++) {
		if ((map & (1 << i)) != 0) {
			printf("%s0x%jx-0x%jx", first ? "" : ", ",
			    i * scale, (i + 1) * scale - 1);
			first = 0;
		}
	}
}

static int
apb_checkrange(uint8_t map, rman_res_t scale, rman_res_t start, rman_res_t end)
{
	int i, ei;

	i = start / scale;
	ei = end / scale;
	if (i > 7 || ei > 7)
		return (0);
	for (; i <= ei; i++)
		if ((map & (1 << i)) == 0)
			return (0);
	return (1);
}

static int
apb_attach(device_t dev)
{
	struct apb_softc *sc;
	struct sysctl_ctx_list *sctx;
	struct sysctl_oid *soid;

	sc = device_get_softc(dev);

	/*
	 * Get current bridge configuration.
	 */
	sc->sc_bsc.ops_pcib_sc.domain = pci_get_domain(dev);
	sc->sc_bsc.ops_pcib_sc.pribus = pci_get_bus(dev);
	pci_write_config(dev, PCIR_PRIBUS_1, sc->sc_bsc.ops_pcib_sc.pribus, 1);
	sc->sc_bsc.ops_pcib_sc.bus.sec =
	    pci_read_config(dev, PCIR_SECBUS_1, 1);
	sc->sc_bsc.ops_pcib_sc.bus.sub =
	    pci_read_config(dev, PCIR_SUBBUS_1, 1);
	sc->sc_bsc.ops_pcib_sc.bridgectl =
	    pci_read_config(dev, PCIR_BRIDGECTL_1, 2);
	sc->sc_iomap = pci_read_config(dev, APBR_IOMAP, 1);
	sc->sc_memmap = pci_read_config(dev, APBR_MEMMAP, 1);

	/*
	 * Setup SYSCTL reporting nodes.
	 */
	sctx = device_get_sysctl_ctx(dev);
	soid = device_get_sysctl_tree(dev);
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "domain",
	    CTLFLAG_RD, &sc->sc_bsc.ops_pcib_sc.domain, 0,
	    "Domain number");
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "pribus",
	    CTLFLAG_RD, &sc->sc_bsc.ops_pcib_sc.pribus, 0,
	    "Primary bus number");
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "secbus",
	    CTLFLAG_RD, &sc->sc_bsc.ops_pcib_sc.bus.sec, 0,
	    "Secondary bus number");
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "subbus",
	    CTLFLAG_RD, &sc->sc_bsc.ops_pcib_sc.bus.sub, 0,
	    "Subordinate bus number");

	ofw_pcib_gen_setup(dev);

	if (bootverbose) {
		device_printf(dev, "  domain            %d\n",
		    sc->sc_bsc.ops_pcib_sc.domain);
		device_printf(dev, "  secondary bus     %d\n",
		    sc->sc_bsc.ops_pcib_sc.bus.sec);
		device_printf(dev, "  subordinate bus   %d\n",
		    sc->sc_bsc.ops_pcib_sc.bus.sub);
		device_printf(dev, "  I/O decode        ");
		apb_map_print(sc->sc_iomap, APB_IO_SCALE);
		printf("\n");
		device_printf(dev, "  memory decode     ");
		apb_map_print(sc->sc_memmap, APB_MEM_SCALE);
		printf("\n");
	}

	device_add_child(dev, "pci", -1);
	return (bus_generic_attach(dev));
}

/*
 * We have to trap resource allocation requests and ensure that the bridge
 * is set up to, or capable of handling them.
 */
static struct resource *
apb_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct apb_softc *sc;

	sc = device_get_softc(dev);

	/*
	 * If this is a "default" allocation against this rid, we can't work
	 * out where it's coming from (we should actually never see these) so
	 * we just have to punt.
	 */
	if (RMAN_IS_DEFAULT_RANGE(start, end)) {
		device_printf(dev, "can't decode default resource id %d for "
		    "%s, bypassing\n", *rid, device_get_nameunit(child));
		goto passup;
	}

	/*
	 * Fail the allocation for this range if it's not supported.
	 * XXX we should probably just fix up the bridge decode and
	 * soldier on.
	 */
	switch (type) {
	case SYS_RES_IOPORT:
		if (!apb_checkrange(sc->sc_iomap, APB_IO_SCALE, start, end)) {
			device_printf(dev, "device %s requested unsupported "
			    "I/O range 0x%jx-0x%jx\n",
			    device_get_nameunit(child), start, end);
			return (NULL);
		}
		if (bootverbose)
			device_printf(sc->sc_bsc.ops_pcib_sc.dev, "device "
			    "%s requested decoded I/O range 0x%jx-0x%jx\n",
			    device_get_nameunit(child), start, end);
		break;
	case SYS_RES_MEMORY:
		if (!apb_checkrange(sc->sc_memmap, APB_MEM_SCALE, start,
		    end)) {
			device_printf(dev, "device %s requested unsupported "
			    "memory range 0x%jx-0x%jx\n",
			    device_get_nameunit(child), start, end);
			return (NULL);
		}
		if (bootverbose)
			device_printf(sc->sc_bsc.ops_pcib_sc.dev, "device "
			    "%s requested decoded memory range 0x%jx-0x%jx\n",
			    device_get_nameunit(child), start, end);
		break;
	}

 passup:
	/*
	 * Bridge is OK decoding this resource, so pass it up.
	 */
	return (bus_generic_alloc_resource(dev, child, type, rid, start, end,
	    count, flags));
}

static int
apb_adjust_resource(device_t dev, device_t child, int type,
    struct resource *r, rman_res_t start, rman_res_t end)
{
	struct apb_softc *sc;

	sc = device_get_softc(dev);
	switch (type) {
	case SYS_RES_IOPORT:
		if (!apb_checkrange(sc->sc_iomap, APB_IO_SCALE, start, end))
			return (ENXIO);
		break;
	case SYS_RES_MEMORY:
		if (!apb_checkrange(sc->sc_memmap, APB_MEM_SCALE, start, end))
			return (ENXIO);
		break;
	}
	return (bus_generic_adjust_resource(dev, child, type, r, start, end));
}
