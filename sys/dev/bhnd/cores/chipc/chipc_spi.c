/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Michael Zhilin <mizhka@gmail.com>
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/rman.h>
#include <sys/bus.h>

#include <machine/bus.h>

#include <dev/bhnd/bhndvar.h>

#include <dev/spibus/spi.h>

#include "bhnd_chipc_if.h"

#include "spibus_if.h"

#include "chipcreg.h"
#include "chipcvar.h"
#include "chipc_slicer.h"

#include "chipc_spi.h"

static int	chipc_spi_probe(device_t dev);
static int	chipc_spi_attach(device_t dev);
static int	chipc_spi_detach(device_t dev);
static int	chipc_spi_transfer(device_t dev, device_t child,
		    struct spi_command *cmd);
static int	chipc_spi_txrx(struct chipc_spi_softc *sc, uint8_t in,
		    uint8_t* out);
static int	chipc_spi_wait(struct chipc_spi_softc *sc);

static int
chipc_spi_probe(device_t dev)
{
	device_set_desc(dev, "Broadcom ChipCommon SPI");
	return (BUS_PROBE_NOWILDCARD);
}

static int
chipc_spi_attach(device_t dev)
{
	struct chipc_spi_softc	*sc;
	struct chipc_caps	*ccaps;
	device_t		 flash_dev;
	device_t		 spibus;
	const char		*flash_name;
	int			 error;

	sc = device_get_softc(dev);

	/* Allocate SPI controller registers */
	sc->sc_rid = 1;
	sc->sc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->sc_rid,
	    RF_ACTIVE);
	if (sc->sc_res == NULL) {
		device_printf(dev, "failed to allocate device registers\n");
		return (ENXIO);
	}

	/* Allocate flash shadow region */
	sc->sc_flash_rid = 0;
	sc->sc_flash_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_flash_rid, RF_ACTIVE);
	if (sc->sc_flash_res == NULL) {
		device_printf(dev, "failed to allocate flash region\n");
		error = ENXIO;
		goto failed;
	}

	/* 
	 * Add flash device
	 * 
	 * XXX: This should be replaced with a DEVICE_IDENTIFY implementation
	 * in chipc-specific subclasses of the mx25l and at45d drivers.
	 */
	if ((spibus = device_add_child(dev, "spibus", -1)) == NULL) {
		device_printf(dev, "failed to add spibus\n");
		error = ENXIO;
		goto failed;
	}

	/* Let spibus perform full attach before we try to call
	 * BUS_ADD_CHILD() */
	if ((error = bus_generic_attach(dev)))
		goto failed;

	/* Determine flash type and add the flash child */
	ccaps = BHND_CHIPC_GET_CAPS(device_get_parent(dev));
	flash_name = chipc_sflash_device_name(ccaps->flash_type);
	if (flash_name != NULL) {
		flash_dev = BUS_ADD_CHILD(spibus, 0, flash_name, -1);
		if (flash_dev == NULL) {
			device_printf(dev, "failed to add %s\n", flash_name);
			error = ENXIO;
			goto failed;
		}

		chipc_register_slicer(ccaps->flash_type);

		if ((error = device_probe_and_attach(flash_dev))) {
			device_printf(dev, "failed to attach %s: %d\n",
			    flash_name, error);
			goto failed;
		}
	}

	return (0);

failed:
	device_delete_children(dev);

	if (sc->sc_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_rid,
		    sc->sc_res);

	if (sc->sc_flash_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_flash_rid,
		    sc->sc_flash_res);

	return (error);
}

static int
chipc_spi_detach(device_t dev)
{
	struct chipc_spi_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	if ((error = bus_generic_detach(dev)))
		return (error);

	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_rid, sc->sc_res);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_flash_rid,
	    sc->sc_flash_res);
	return (0);
}

static int
chipc_spi_wait(struct chipc_spi_softc *sc)
{
	int i;

	for (i = CHIPC_SPI_MAXTRIES; i > 0; i--)
		if (!(SPI_READ(sc, CHIPC_SPI_FLASHCTL) & CHIPC_SPI_FLASHCTL_START))
			break;

	if (i > 0)
		return (0);

	BHND_WARN_DEV(sc->sc_dev, "busy: CTL=0x%x DATA=0x%x",
	    SPI_READ(sc, CHIPC_SPI_FLASHCTL),
	    SPI_READ(sc, CHIPC_SPI_FLASHDATA));
	return (-1);
}

static int
chipc_spi_txrx(struct chipc_spi_softc *sc, uint8_t out, uint8_t* in)
{
	uint32_t ctl;

	ctl = CHIPC_SPI_FLASHCTL_START | CHIPC_SPI_FLASHCTL_CSACTIVE | out;
	SPI_BARRIER_WRITE(sc);
	SPI_WRITE(sc, CHIPC_SPI_FLASHCTL, ctl);
	SPI_BARRIER_WRITE(sc);

	if (chipc_spi_wait(sc))
		return (-1);

	*in = SPI_READ(sc, CHIPC_SPI_FLASHDATA) & 0xff;
	return (0);
}

static int
chipc_spi_transfer(device_t dev, device_t child, struct spi_command *cmd)
{
	struct chipc_spi_softc	*sc;
	uint8_t		*buf_in;
	uint8_t		*buf_out;
	int		 i;

	sc = device_get_softc(dev);
	KASSERT(cmd->tx_cmd_sz == cmd->rx_cmd_sz,
	    ("TX/RX command sizes should be equal"));
	KASSERT(cmd->tx_data_sz == cmd->rx_data_sz,
	    ("TX/RX data sizes should be equal"));

	if (cmd->tx_cmd_sz == 0) {
		BHND_DEBUG_DEV(child, "size of command is ZERO");
		return (EIO);
	}

	SPI_BARRIER_WRITE(sc);
	SPI_WRITE(sc, CHIPC_SPI_FLASHADDR, 0);
	SPI_BARRIER_WRITE(sc);

	/*
	 * Transfer command
	 */
	buf_out = (uint8_t *)cmd->tx_cmd;
	buf_in = (uint8_t *)cmd->rx_cmd;
	for (i = 0; i < cmd->tx_cmd_sz; i++)
		 if (chipc_spi_txrx(sc, buf_out[i], &(buf_in[i])))
			 return (EIO);

	/*
	 * Receive/transmit data
	 */
	buf_out = (uint8_t *)cmd->tx_data;
	buf_in = (uint8_t *)cmd->rx_data;
	for (i = 0; i < cmd->tx_data_sz; i++)
		if (chipc_spi_txrx(sc, buf_out[i], &(buf_in[i])))
			return (EIO);

	/*
	 * Clear CS bit and whole control register
	 */
	SPI_BARRIER_WRITE(sc);
	SPI_WRITE(sc, CHIPC_SPI_FLASHCTL, 0);
	SPI_BARRIER_WRITE(sc);

	return (0);
}

static device_method_t chipc_spi_methods[] = {
		DEVMETHOD(device_probe,		chipc_spi_probe),
		DEVMETHOD(device_attach,	chipc_spi_attach),
		DEVMETHOD(device_detach,	chipc_spi_detach),

		/* SPI */
		DEVMETHOD(spibus_transfer,	chipc_spi_transfer),
		DEVMETHOD_END
};

static driver_t chipc_spi_driver = {
	"spi",
	chipc_spi_methods,
	sizeof(struct chipc_spi_softc),
};

static devclass_t chipc_spi_devclass;

DRIVER_MODULE(chipc_spi, bhnd_chipc, chipc_spi_driver, chipc_spi_devclass,
    0, 0);
