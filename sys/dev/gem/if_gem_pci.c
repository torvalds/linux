/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (C) 2001 Eduardo Horvath.
 * Copyright (c) 2007 Marius Strobl <marius@FreeBSD.org>
 * All rights reserved.
 *
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: if_gem_pci.c,v 1.7 2001/10/18 15:09:15 thorpej Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * PCI bindings for Apple GMAC, Sun ERI and Sun GEM Ethernet controllers
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/socket.h>

#include <net/ethernet.h>
#include <net/if.h>

#include <machine/bus.h>
#if defined(__powerpc__) || defined(__sparc64__)
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>
#endif
#include <machine/resource.h>

#include <dev/gem/if_gemreg.h>
#include <dev/gem/if_gemvar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "miibus_if.h"

static int	gem_pci_attach(device_t dev);
static int	gem_pci_detach(device_t dev);
static int	gem_pci_probe(device_t dev);
static int	gem_pci_resume(device_t dev);
static int	gem_pci_suspend(device_t dev);

static const struct gem_pci_dev {
	uint32_t	gpd_devid;
	int		gpd_variant;
	const char	*gpd_desc;
} gem_pci_devlist[] = {
	{ 0x1101108e, GEM_SUN_ERI,	"Sun ERI 10/100 Ethernet" },
	{ 0x2bad108e, GEM_SUN_GEM,	"Sun GEM Gigabit Ethernet" },
	{ 0x0021106b, GEM_APPLE_GMAC,	"Apple UniNorth GMAC Ethernet" },
	{ 0x0024106b, GEM_APPLE_GMAC,	"Apple Pangea GMAC Ethernet" },
	{ 0x0032106b, GEM_APPLE_GMAC,	"Apple UniNorth2 GMAC Ethernet" },
	{ 0x004c106b, GEM_APPLE_K2_GMAC,"Apple K2 GMAC Ethernet" },
	{ 0x0051106b, GEM_APPLE_GMAC,	"Apple Shasta GMAC Ethernet" },
	{ 0x006b106b, GEM_APPLE_GMAC,	"Apple Intrepid 2 GMAC Ethernet" },
	{ 0, 0, NULL }
};

static device_method_t gem_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		gem_pci_probe),
	DEVMETHOD(device_attach,	gem_pci_attach),
	DEVMETHOD(device_detach,	gem_pci_detach),
	DEVMETHOD(device_suspend,	gem_pci_suspend),
	DEVMETHOD(device_resume,	gem_pci_resume),
	/* Use the suspend handler here, it is all that is required. */
	DEVMETHOD(device_shutdown,	gem_pci_suspend),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	gem_mii_readreg),
	DEVMETHOD(miibus_writereg,	gem_mii_writereg),
	DEVMETHOD(miibus_statchg,	gem_mii_statchg),

	DEVMETHOD_END
};

static driver_t gem_pci_driver = {
	"gem",
	gem_pci_methods,
	sizeof(struct gem_softc)
};

DRIVER_MODULE(gem, pci, gem_pci_driver, gem_devclass, 0, 0);
MODULE_PNP_INFO("W32:vendor/device", pci, gem, gem_pci_devlist,
    nitems(gem_pci_devlist) - 1);
MODULE_DEPEND(gem, pci, 1, 1, 1);
MODULE_DEPEND(gem, ether, 1, 1, 1);

static int
gem_pci_probe(device_t dev)
{
	int i;

	for (i = 0; gem_pci_devlist[i].gpd_desc != NULL; i++) {
		if (pci_get_devid(dev) == gem_pci_devlist[i].gpd_devid) {
			device_set_desc(dev, gem_pci_devlist[i].gpd_desc);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static struct resource_spec gem_pci_res_spec[] = {
	{ SYS_RES_IRQ, 0, RF_SHAREABLE | RF_ACTIVE },	/* GEM_RES_INTR */
	{ SYS_RES_MEMORY, PCIR_BAR(0), RF_ACTIVE },	/* GEM_RES_BANK1 */
	{ -1, 0 }
};

#define	GEM_SHARED_PINS		"shared-pins"
#define	GEM_SHARED_PINS_SERDES	"serdes"

static int
gem_pci_attach(device_t dev)
{
	struct gem_softc *sc;
	int i;
#if defined(__powerpc__) || defined(__sparc64__)
	char buf[sizeof(GEM_SHARED_PINS)];
#else
	int j;
#endif

	sc = device_get_softc(dev);
	sc->sc_variant = GEM_UNKNOWN;
	for (i = 0; gem_pci_devlist[i].gpd_desc != NULL; i++) {
		if (pci_get_devid(dev) == gem_pci_devlist[i].gpd_devid) {
			sc->sc_variant = gem_pci_devlist[i].gpd_variant;
			break;
		}
	}
	if (sc->sc_variant == GEM_UNKNOWN) {
		device_printf(dev, "unknown adaptor\n");
		return (ENXIO);
	}

	pci_enable_busmaster(dev);

	/*
	 * Some Sun GEMs/ERIs do have their intpin register bogusly set to 0,
	 * although it should be 1.  Correct that.
	 */
	if (pci_get_intpin(dev) == 0)
		pci_set_intpin(dev, 1);

	/* Set the PCI latency timer for Sun ERIs. */
	if (sc->sc_variant == GEM_SUN_ERI)
		pci_write_config(dev, PCIR_LATTIMER, GEM_ERI_LATENCY_TIMER, 1);

	sc->sc_dev = dev;
	sc->sc_flags |= GEM_PCI;

	if (bus_alloc_resources(dev, gem_pci_res_spec, sc->sc_res)) {
		device_printf(dev, "failed to allocate resources\n");
		bus_release_resources(dev, gem_pci_res_spec, sc->sc_res);
		return (ENXIO);
	}

	GEM_LOCK_INIT(sc, device_get_nameunit(dev));

	/*
	 * Derive GEM_RES_BANK2 from GEM_RES_BANK1.  This seemed cleaner
	 * with the old way of using copies of the bus tag and handle in
	 * the softc along with bus_space_*()...
	 */
	sc->sc_res[GEM_RES_BANK2] = malloc(sizeof(*sc->sc_res[GEM_RES_BANK2]),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_res[GEM_RES_BANK2] == NULL) {
		device_printf(dev, "failed to allocate bank2 resource\n");
		goto fail;
	}
	rman_set_bustag(sc->sc_res[GEM_RES_BANK2],
	    rman_get_bustag(sc->sc_res[GEM_RES_BANK1]));
	bus_space_subregion(rman_get_bustag(sc->sc_res[GEM_RES_BANK1]),
	    rman_get_bushandle(sc->sc_res[GEM_RES_BANK1]),
	    GEM_PCI_BANK2_OFFSET, GEM_PCI_BANK2_SIZE,
	    &sc->sc_res[GEM_RES_BANK2]->r_bushandle);

	/* Determine whether we're running at 66MHz. */
	if ((GEM_BANK2_READ_4(sc, GEM_PCI_BIF_CONFIG) &
	   GEM_PCI_BIF_CNF_M66EN) != 0)
		sc->sc_flags |= GEM_PCI66;

#if defined(__powerpc__) || defined(__sparc64__)
	OF_getetheraddr(dev, sc->sc_enaddr);
	if (OF_getprop(ofw_bus_get_node(dev), GEM_SHARED_PINS, buf,
	    sizeof(buf)) > 0) {
		buf[sizeof(buf) - 1] = '\0';
		if (strcmp(buf, GEM_SHARED_PINS_SERDES) == 0)
			sc->sc_flags |= GEM_SERDES;
	}
#else
	/*
	 * Dig out VPD (vital product data) and read NA (network address).
	 * The VPD resides in the PCI Expansion ROM (PCI FCode) and can't
	 * be accessed via the PCI capability pointer.
	 * ``Writing FCode 3.x Programs'' (newer ones, dated 1997 and later)
	 * chapter 2 describes the data structure.
	 */

#define	PCI_ROMHDR_SIZE			0x1c
#define	PCI_ROMHDR_SIG			0x00
#define	PCI_ROMHDR_SIG_MAGIC		0xaa55		/* little endian */
#define	PCI_ROMHDR_PTR_DATA		0x18
#define	PCI_ROM_SIZE			0x18
#define	PCI_ROM_SIG			0x00
#define	PCI_ROM_SIG_MAGIC		0x52494350	/* "PCIR", endian */
							/* reversed */
#define	PCI_ROM_VENDOR			0x04
#define	PCI_ROM_DEVICE			0x06
#define	PCI_ROM_PTR_VPD			0x08
#define	PCI_VPDRES_BYTE0		0x00
#define	PCI_VPDRES_ISLARGE(x)		((x) & 0x80)
#define	PCI_VPDRES_LARGE_NAME(x)	((x) & 0x7f)
#define	PCI_VPDRES_LARGE_LEN_LSB	0x01
#define	PCI_VPDRES_LARGE_LEN_MSB	0x02
#define	PCI_VPDRES_LARGE_SIZE		0x03
#define	PCI_VPDRES_TYPE_VPD		0x10		/* large */
#define	PCI_VPD_KEY0			0x00
#define	PCI_VPD_KEY1			0x01
#define	PCI_VPD_LEN			0x02
#define	PCI_VPD_SIZE			0x03

#define	GEM_ROM_READ_1(sc, offs)					\
	GEM_BANK1_READ_1((sc), GEM_PCI_ROM_OFFSET + (offs))
#define	GEM_ROM_READ_2(sc, offs)					\
	GEM_BANK1_READ_2((sc), GEM_PCI_ROM_OFFSET + (offs))
#define	GEM_ROM_READ_4(sc, offs)					\
	GEM_BANK1_READ_4((sc), GEM_PCI_ROM_OFFSET + (offs))

	/* Read PCI Expansion ROM header. */
	if (GEM_ROM_READ_2(sc, PCI_ROMHDR_SIG) != PCI_ROMHDR_SIG_MAGIC ||
	    (i = GEM_ROM_READ_2(sc, PCI_ROMHDR_PTR_DATA)) <
	    PCI_ROMHDR_SIZE) {
		device_printf(dev, "unexpected PCI Expansion ROM header\n");
		goto fail;
	}

	/* Read PCI Expansion ROM data. */
	if (GEM_ROM_READ_4(sc, i + PCI_ROM_SIG) != PCI_ROM_SIG_MAGIC ||
	    GEM_ROM_READ_2(sc, i + PCI_ROM_VENDOR) != pci_get_vendor(dev) ||
	    GEM_ROM_READ_2(sc, i + PCI_ROM_DEVICE) != pci_get_device(dev) ||
	    (j = GEM_ROM_READ_2(sc, i + PCI_ROM_PTR_VPD)) <
	    i + PCI_ROM_SIZE) {
		device_printf(dev, "unexpected PCI Expansion ROM data\n");
		goto fail;
	}

	/*
	 * Read PCI VPD.
	 * SUNW,pci-gem cards have a single large resource VPD-R tag
	 * containing one NA.  The VPD used is not in PCI 2.2 standard
	 * format however.  The length in the resource header is in big
	 * endian and the end tag is non-standard (0x79) and followed
	 * by an all-zero "checksum" byte.  Sun calls this a "Fresh
	 * Choice Ethernet" VPD...
	 */
	if (PCI_VPDRES_ISLARGE(GEM_ROM_READ_1(sc,
	    j + PCI_VPDRES_BYTE0)) == 0 ||
	    PCI_VPDRES_LARGE_NAME(GEM_ROM_READ_1(sc,
	    j + PCI_VPDRES_BYTE0)) != PCI_VPDRES_TYPE_VPD ||
	    ((GEM_ROM_READ_1(sc, j + PCI_VPDRES_LARGE_LEN_LSB) << 8) |
	    GEM_ROM_READ_1(sc, j + PCI_VPDRES_LARGE_LEN_MSB)) !=
	    PCI_VPD_SIZE + ETHER_ADDR_LEN ||
	    GEM_ROM_READ_1(sc, j + PCI_VPDRES_LARGE_SIZE + PCI_VPD_KEY0) !=
	    0x4e /* N */ ||
	    GEM_ROM_READ_1(sc, j + PCI_VPDRES_LARGE_SIZE + PCI_VPD_KEY1) !=
	    0x41 /* A */ ||
	    GEM_ROM_READ_1(sc, j + PCI_VPDRES_LARGE_SIZE + PCI_VPD_LEN) !=
	    ETHER_ADDR_LEN ||
	    GEM_ROM_READ_1(sc, j + PCI_VPDRES_LARGE_SIZE + PCI_VPD_SIZE +
	    ETHER_ADDR_LEN) != 0x79) {
		device_printf(dev, "unexpected PCI VPD\n");
		goto fail;
	}
	bus_read_region_1(sc->sc_res[GEM_RES_BANK1],
	    GEM_PCI_ROM_OFFSET + j + PCI_VPDRES_LARGE_SIZE + PCI_VPD_SIZE,
	    sc->sc_enaddr, ETHER_ADDR_LEN);
#endif
	/*
	 * The Xserve G5 has a fake GMAC with an all-zero MAC address.
	 * Check for this, and don't attach in this case.
	 */

	for (i = 0; i < ETHER_ADDR_LEN && sc->sc_enaddr[i] == 0; i++) {}
	if (i == ETHER_ADDR_LEN) {
		device_printf(dev, "invalid MAC address\n");
		goto fail;
	}

	if (gem_attach(sc) != 0) {
		device_printf(dev, "could not be attached\n");
		goto fail;
	}

	if (bus_setup_intr(dev, sc->sc_res[GEM_RES_INTR], INTR_TYPE_NET |
	    INTR_MPSAFE, NULL, gem_intr, sc, &sc->sc_ih) != 0) {
		device_printf(dev, "failed to set up interrupt\n");
		gem_detach(sc);
		goto fail;
	}
	return (0);

 fail:
	if (sc->sc_res[GEM_RES_BANK2] != NULL)
		free(sc->sc_res[GEM_RES_BANK2], M_DEVBUF);
	GEM_LOCK_DESTROY(sc);
	bus_release_resources(dev, gem_pci_res_spec, sc->sc_res);
	return (ENXIO);
}

static int
gem_pci_detach(device_t dev)
{
	struct gem_softc *sc;

	sc = device_get_softc(dev);
	bus_teardown_intr(dev, sc->sc_res[GEM_RES_INTR], sc->sc_ih);
	gem_detach(sc);
	free(sc->sc_res[GEM_RES_BANK2], M_DEVBUF);
	GEM_LOCK_DESTROY(sc);
	bus_release_resources(dev, gem_pci_res_spec, sc->sc_res);
	return (0);
}

static int
gem_pci_suspend(device_t dev)
{

	gem_suspend(device_get_softc(dev));
	return (0);
}

static int
gem_pci_resume(device_t dev)
{

	gem_resume(device_get_softc(dev));
	return (0);
}
