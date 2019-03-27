/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994,1995 Stefan Esser, Wolfgang StanglMeier
 * Copyright (c) 2000 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2000 BSDi
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * PCI:ISA bridge support
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <isa/isavar.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

static int	isab_pci_probe(device_t dev);
static int	isab_pci_attach(device_t dev);
static struct resource *	isab_pci_alloc_resource(device_t dev,
    device_t child, int type, int *rid, rman_res_t start, rman_res_t end,
    rman_res_t count, u_int flags);
static int	isab_pci_release_resource(device_t dev, device_t child,
    int type, int rid, struct resource *r);

static device_method_t isab_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,		isab_pci_probe),
    DEVMETHOD(device_attach,		isab_pci_attach),
    DEVMETHOD(device_detach,		bus_generic_detach),
    DEVMETHOD(device_shutdown,		bus_generic_shutdown),
    DEVMETHOD(device_suspend,		bus_generic_suspend),
    DEVMETHOD(device_resume,		bus_generic_resume),

    /* Bus interface */
    DEVMETHOD(bus_add_child,		bus_generic_add_child),
    DEVMETHOD(bus_alloc_resource,	isab_pci_alloc_resource),
    DEVMETHOD(bus_release_resource,	isab_pci_release_resource),
    DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
    DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),
    DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
    DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

    DEVMETHOD_END
};

struct isab_pci_resource {
	struct resource	*ip_res;
	int	ip_refs;
};

struct isab_pci_softc {
	struct isab_pci_resource isab_pci_res[PCIR_MAX_BAR_0 + 1];
};

static driver_t isab_driver = {
    "isab",
    isab_methods,
    sizeof(struct isab_pci_softc),
};

DRIVER_MODULE(isab, pci, isab_driver, isab_devclass, 0, 0);

/*
 * XXX we need to add a quirk list here for bridges that don't correctly
 *     report themselves.
 */
static int
isab_pci_probe(device_t dev)
{
    int		matched = 0;

    /*
     * Try for a generic match based on class/subclass.
     */
    if ((pci_get_class(dev) == PCIC_BRIDGE) &&
	(pci_get_subclass(dev) == PCIS_BRIDGE_ISA)) {
	matched = 1;
    } else {
	/*
	 * These are devices that we *know* are PCI:ISA bridges. 
	 * Sometimes, however, they don't report themselves as
	 * such.  Check in case one of them is pretending to be
	 * something else.
	 */
	switch (pci_get_devid(dev)) {
	case 0x04848086:	/* Intel 82378ZB/82378IB */
	case 0x122e8086:	/* Intel 82371FB */
	case 0x70008086:	/* Intel 82371SB */
	case 0x71108086:	/* Intel 82371AB */
	case 0x71988086:	/* Intel 82443MX */
	case 0x24108086:	/* Intel 82801AA (ICH) */
	case 0x24208086:	/* Intel 82801AB (ICH0) */
	case 0x24408086:	/* Intel 82801AB (ICH2) */
	case 0x00061004:	/* VLSI 82C593 */
	case 0x05861106:	/* VIA 82C586 */
	case 0x05961106:	/* VIA 82C596 */
	case 0x06861106:	/* VIA 82C686 */
	case 0x153310b9:	/* AcerLabs M1533 */
	case 0x154310b9:	/* AcerLabs M1543 */
	case 0x00081039:	/* SiS 85c503 */
	case 0x00001078:	/* Cyrix Cx5510 */
	case 0x01001078:	/* Cyrix Cx5530 */
	case 0xc7001045:	/* OPTi 82C700 (FireStar) */
	case 0x886a1060:	/* UMC UM8886 ISA */
	case 0x02001166:	/* ServerWorks IB6566 PCI */
	    if (bootverbose)
		printf("PCI-ISA bridge with incorrect subclass 0x%x\n",
		       pci_get_subclass(dev));
	    matched = 1;
	    break;
	
	default:
	    break;
	}
    }

    if (matched) {
	device_set_desc(dev, "PCI-ISA bridge");
	return(-10000);
    }
    return(ENXIO);
}

static int
isab_pci_attach(device_t dev)
{

	bus_generic_probe(dev);
	return (isab_attach(dev));
}

static struct resource *
isab_pci_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct isab_pci_softc *sc;
	int bar;

	if (device_get_parent(child) != dev)
		return bus_generic_alloc_resource(dev, child, type, rid, start,
		    end, count, flags);

	switch (type) {
	case SYS_RES_MEMORY:
	case SYS_RES_IOPORT:
		/*
		 * For BARs, we cache the resource so that we only allocate it
		 * from the PCI bus once.
		 */
		bar = PCI_RID2BAR(*rid);
		if (bar < 0 || bar > PCIR_MAX_BAR_0)
			return (NULL);
		sc = device_get_softc(dev);
		if (sc->isab_pci_res[bar].ip_res == NULL)
			sc->isab_pci_res[bar].ip_res = bus_alloc_resource(dev, type,
			    rid, start, end, count, flags);
		if (sc->isab_pci_res[bar].ip_res != NULL)
			sc->isab_pci_res[bar].ip_refs++;
		return (sc->isab_pci_res[bar].ip_res);
	}

	return (BUS_ALLOC_RESOURCE(device_get_parent(dev), child, type, rid,
		start, end, count, flags));
}

static int
isab_pci_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{
	struct isab_pci_softc *sc;
	int bar, error;

	if (device_get_parent(child) != dev)
		return bus_generic_release_resource(dev, child, type, rid, r);

	switch (type) {
	case SYS_RES_MEMORY:
	case SYS_RES_IOPORT:
		/*
		 * For BARs, we release the resource from the PCI bus
		 * when the last child reference goes away.
		 */
		bar = PCI_RID2BAR(rid);
		if (bar < 0 || bar > PCIR_MAX_BAR_0)
			return (EINVAL);
		sc = device_get_softc(dev);
		if (sc->isab_pci_res[bar].ip_res == NULL)
			return (EINVAL);
		KASSERT(sc->isab_pci_res[bar].ip_res == r,
		    ("isa_pci resource mismatch"));
		if (sc->isab_pci_res[bar].ip_refs > 1) {
			sc->isab_pci_res[bar].ip_refs--;
			return (0);
		}
		KASSERT(sc->isab_pci_res[bar].ip_refs > 0,
		    ("isa_pci resource reference count underflow"));
		error = bus_release_resource(dev, type, rid, r);
		if (error == 0) {
			sc->isab_pci_res[bar].ip_res = NULL;
			sc->isab_pci_res[bar].ip_refs = 0;
		}
		return (error);
	}

	return (BUS_RELEASE_RESOURCE(device_get_parent(dev), child, type,
		rid, r));
}
