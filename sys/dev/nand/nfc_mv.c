/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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

/* Integrated NAND controller driver */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/time.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <arm/mv/mvvar.h>
#include <arm/mv/mvwin.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/nand/nand.h>
#include <dev/nand/nandbus.h>
#include "nfc_if.h"

#define MV_NAND_DATA	(0x00)
#define MV_NAND_COMMAND	(0x01)
#define MV_NAND_ADDRESS	(0x02)

struct mv_nand_softc {
	struct nand_softc 	nand_dev;
	bus_space_handle_t 	sc_handle;
	bus_space_tag_t		sc_tag;
	struct resource		*res;
	int			rid;
};

static int	mv_nand_attach(device_t);
static int	mv_nand_probe(device_t);
static int	mv_nand_send_command(device_t, uint8_t);
static int	mv_nand_send_address(device_t, uint8_t);
static uint8_t	mv_nand_read_byte(device_t);
static void	mv_nand_read_buf(device_t, void *, uint32_t);
static void	mv_nand_write_buf(device_t, void *, uint32_t);
static int	mv_nand_select_cs(device_t, uint8_t);
static int	mv_nand_read_rnb(device_t);

static device_method_t mv_nand_methods[] = {
	DEVMETHOD(device_probe,		mv_nand_probe),
	DEVMETHOD(device_attach,	mv_nand_attach),

	DEVMETHOD(nfc_send_command,	mv_nand_send_command),
	DEVMETHOD(nfc_send_address,	mv_nand_send_address),
	DEVMETHOD(nfc_read_byte,	mv_nand_read_byte),
	DEVMETHOD(nfc_read_buf,		mv_nand_read_buf),
	DEVMETHOD(nfc_write_buf,	mv_nand_write_buf),
	DEVMETHOD(nfc_select_cs,	mv_nand_select_cs),
	DEVMETHOD(nfc_read_rnb,		mv_nand_read_rnb),

	{ 0, 0 },
};

static driver_t mv_nand_driver = {
	"nand",
	mv_nand_methods,
	sizeof(struct mv_nand_softc),
};

static devclass_t mv_nand_devclass;
DRIVER_MODULE(mv_nand, localbus, mv_nand_driver, mv_nand_devclass, 0, 0);

static int
mv_nand_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "mrvl,nfc"))
		return (ENXIO);

	device_set_desc(dev, "Marvell NAND controller");
	return (BUS_PROBE_DEFAULT);
}

static int
mv_nand_attach(device_t dev)
{
	struct mv_nand_softc *sc;
	int err;

	sc = device_get_softc(dev);
	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid,
	    RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "could not allocate resources!\n");
		return (ENXIO);
	}

	sc->sc_tag = rman_get_bustag(sc->res);
	sc->sc_handle = rman_get_bushandle(sc->res);

	nand_init(&sc->nand_dev, dev, NAND_ECC_SOFT, 0, 0, NULL, NULL);

	err = nandbus_create(dev);

	return (err);
}

static int
mv_nand_send_command(device_t dev, uint8_t command)
{
	struct mv_nand_softc *sc;

	nand_debug(NDBG_DRV,"mv_nand: send command %x", command);

	sc = device_get_softc(dev);
	bus_space_write_1(sc->sc_tag, sc->sc_handle, MV_NAND_COMMAND, command);
	return (0);
}

static int
mv_nand_send_address(device_t dev, uint8_t addr)
{
	struct mv_nand_softc *sc;

	nand_debug(NDBG_DRV,"mv_nand: send address %x", addr);

	sc = device_get_softc(dev);
	bus_space_write_1(sc->sc_tag, sc->sc_handle, MV_NAND_ADDRESS, addr);
	return (0);
}

static uint8_t
mv_nand_read_byte(device_t dev)
{
	struct mv_nand_softc *sc;
	uint8_t data;

	sc = device_get_softc(dev);
	data = bus_space_read_1(sc->sc_tag, sc->sc_handle, MV_NAND_DATA);

	nand_debug(NDBG_DRV,"mv_nand: read %x", data);

	return (data);
}

static void
mv_nand_read_buf(device_t dev, void* buf, uint32_t len)
{
	struct mv_nand_softc *sc;
	int i;
	uint8_t *b = (uint8_t*)buf;

	sc = device_get_softc(dev);

	for (i = 0; i < len; i++) {
		b[i] = bus_space_read_1(sc->sc_tag, sc->sc_handle,
		    MV_NAND_DATA);
#ifdef NAND_DEBUG
		if (!(i % 16))
			printf("%s", i == 0 ? "mv_nand:\n" : "\n");
		printf(" %x", b[i]);
		if (i == len - 1)
			printf("\n");
#endif
	}
}

static void
mv_nand_write_buf(device_t dev, void* buf, uint32_t len)
{
	struct mv_nand_softc *sc;
	int i;
	uint8_t *b = (uint8_t*)buf;

	sc = device_get_softc(dev);

	for (i = 0; i < len; i++) {
#ifdef NAND_DEBUG
		if (!(i % 16))
			printf("%s", i == 0 ? "mv_nand:\n" : "\n");
		printf(" %x", b[i]);
		if (i == len - 1)
			printf("\n");
#endif
		bus_space_write_1(sc->sc_tag, sc->sc_handle, MV_NAND_DATA,
		    b[i]);
	}
}

static int
mv_nand_select_cs(device_t dev, uint8_t cs)
{

	if (cs > 0)
		return (ENODEV);

	return (0);
}

static int
mv_nand_read_rnb(device_t dev)
{

	/* no-op */
	return (0); /* ready */
}
