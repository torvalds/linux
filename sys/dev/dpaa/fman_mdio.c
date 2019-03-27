/*-
 * Copyright (c) 2016 Justin Hibbits
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/socket.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_var.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <contrib/ncsw/inc/Peripherals/fm_ext.h>

#include "fman.h"
#include "miibus_if.h"

#define MDIO_LOCK()	mtx_lock(&sc->sc_lock)
#define MDIO_UNLOCK()	mtx_unlock(&sc->sc_lock)
#define	MDIO_WRITE4(sc,r,v) \
	bus_space_write_4(&bs_be_tag, sc->sc_handle, sc->sc_offset + r, v)
#define	MDIO_READ4(sc, r) \
	bus_space_read_4(&bs_be_tag, sc->sc_handle, sc->sc_offset + r)

#define	MDIO_MIIMCFG	0x0
#define	MDIO_MIIMCOM	0x4
#define	  MIIMCOM_SCAN_CYCLE	  0x00000002
#define	  MIIMCOM_READ_CYCLE	  0x00000001
#define	MDIO_MIIMADD	0x8
#define	MDIO_MIIMCON	0xc
#define	MDIO_MIIMSTAT	0x10
#define	MDIO_MIIMIND	0x14
#define	  MIIMIND_BUSY	  0x1

static int pqmdio_fdt_probe(device_t dev);
static int pqmdio_fdt_attach(device_t dev);
static int pqmdio_detach(device_t dev);
static int pqmdio_miibus_readreg(device_t dev, int phy, int reg);
static int pqmdio_miibus_writereg(device_t dev, int phy, int reg, int value);

struct pqmdio_softc {
	struct mtx sc_lock;
	bus_space_handle_t sc_handle;
	int sc_offset;
};

static device_method_t pqmdio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pqmdio_fdt_probe),
	DEVMETHOD(device_attach,	pqmdio_fdt_attach),
	DEVMETHOD(device_detach,	pqmdio_detach),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	pqmdio_miibus_readreg),
	DEVMETHOD(miibus_writereg,	pqmdio_miibus_writereg),

	{ 0, 0 }
};

static struct ofw_compat_data mdio_compat_data[] = {
	{"fsl,fman-mdio", 0},
	{NULL, 0}
};

static driver_t pqmdio_driver = {
	"pq_mdio",
	pqmdio_methods,
	sizeof(struct pqmdio_softc),
};

static int
pqmdio_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, mdio_compat_data)->ocd_str)
		return (ENXIO);

	device_set_desc(dev, "Freescale QorIQ MDIO");

	return (BUS_PROBE_DEFAULT);
}

static int
pqmdio_fdt_attach(device_t dev)
{
	struct pqmdio_softc *sc;
	rman_res_t start, count;

	sc = device_get_softc(dev);

	fman_get_bushandle(device_get_parent(dev), &sc->sc_handle);
	bus_get_resource(dev, SYS_RES_MEMORY, 0, &start, &count);
	sc->sc_offset = start;

	mtx_init(&sc->sc_lock, device_get_nameunit(dev), "QorIQ MDIO lock",
	    MTX_DEF);

	return (0);
}

static int
pqmdio_detach(device_t dev)
{
	struct pqmdio_softc *sc;

	sc = device_get_softc(dev);

	mtx_destroy(&sc->sc_lock);

	return (0);
}

int
pqmdio_miibus_readreg(device_t dev, int phy, int reg)
{
	struct pqmdio_softc *sc;
	int                  rv;

	sc = device_get_softc(dev);

	MDIO_LOCK();

	MDIO_WRITE4(sc, MDIO_MIIMADD, (phy << 8) | reg);
	MDIO_WRITE4(sc, MDIO_MIIMCOM, MIIMCOM_READ_CYCLE);

	MDIO_READ4(sc, MDIO_MIIMCOM);

	while ((MDIO_READ4(sc, MDIO_MIIMIND)) & MIIMIND_BUSY)
		;

	rv = MDIO_READ4(sc, MDIO_MIIMSTAT);

	MDIO_WRITE4(sc, MDIO_MIIMCOM, 0);
	MDIO_READ4(sc, MDIO_MIIMCOM);
	MDIO_UNLOCK();

	return (rv);
}

int
pqmdio_miibus_writereg(device_t dev, int phy, int reg, int value)
{
	struct pqmdio_softc *sc;

	sc = device_get_softc(dev);

	MDIO_LOCK();
	/* Stop the MII management read cycle */
	MDIO_WRITE4(sc, MDIO_MIIMCOM, 0);
	MDIO_READ4(sc, MDIO_MIIMCOM);

	MDIO_WRITE4(sc, MDIO_MIIMADD, (phy << 8) | reg);

	MDIO_WRITE4(sc, MDIO_MIIMCON, value);
	MDIO_READ4(sc, MDIO_MIIMCON);

	/* Wait till MII management write is complete */
	while ((MDIO_READ4(sc, MDIO_MIIMIND)) & MIIMIND_BUSY)
		;
	MDIO_UNLOCK();

	return (0);
}

static devclass_t pqmdio_devclass;
DRIVER_MODULE(pqmdio, fman, pqmdio_driver, pqmdio_devclass, 0, 0);
DRIVER_MODULE(miibus, pqmdio, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(pqmdio, miibus, 1, 1, 1);

