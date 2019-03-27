/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
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

#include <dev/spibus/spi.h>
#include <dev/spibus/spibusvar.h>
#include "spibus_if.h"

#include <mips/atheros/ar71xxreg.h>

#undef AR71XX_SPI_DEBUG
#ifdef AR71XX_SPI_DEBUG
#define dprintf printf
#else
#define dprintf(x, arg...)
#endif

/*
 * register space access macros
 */

#define	SPI_BARRIER_WRITE(sc)		bus_barrier((sc)->sc_mem_res, 0, 0, 	\
					    BUS_SPACE_BARRIER_WRITE)
#define	SPI_BARRIER_READ(sc)	bus_barrier((sc)->sc_mem_res, 0, 0, 	\
					    BUS_SPACE_BARRIER_READ)
#define	SPI_BARRIER_RW(sc)		bus_barrier((sc)->sc_mem_res, 0, 0, 	\
					    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE)

#define SPI_WRITE(sc, reg, val)	do {				\
		bus_write_4(sc->sc_mem_res, (reg), (val));	\
	} while (0)

#define SPI_READ(sc, reg)	 bus_read_4(sc->sc_mem_res, (reg))

#define SPI_SET_BITS(sc, reg, bits)	\
	SPI_WRITE(sc, reg, SPI_READ(sc, (reg)) | (bits))

#define SPI_CLEAR_BITS(sc, reg, bits)	\
	SPI_WRITE(sc, reg, SPI_READ(sc, (reg)) & ~(bits))

struct ar71xx_spi_softc {
	device_t		sc_dev;
	struct resource		*sc_mem_res;
	uint32_t		sc_reg_ctrl;
};

static int
ar71xx_spi_probe(device_t dev)
{
	device_set_desc(dev, "AR71XX SPI");
	return (BUS_PROBE_NOWILDCARD);
}

static int
ar71xx_spi_attach(device_t dev)
{
	struct ar71xx_spi_softc *sc = device_get_softc(dev);
	int rid;

	sc->sc_dev = dev;
        rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, 
	    RF_ACTIVE);
	if (!sc->sc_mem_res) {
		device_printf(dev, "Could not map memory\n");
		return (ENXIO);
	}

	SPI_WRITE(sc, AR71XX_SPI_FS, 1);

	/* Flush out read before reading the control register */
	SPI_BARRIER_WRITE(sc);

	sc->sc_reg_ctrl  = SPI_READ(sc, AR71XX_SPI_CTRL);

	/*
	 * XXX TODO: document what the SPI control register does.
	 */
	SPI_WRITE(sc, AR71XX_SPI_CTRL, 0x43);

	/*
	 * Ensure the config register write has gone out before configuring
	 * the chip select mask.
	 */
	SPI_BARRIER_WRITE(sc);
	SPI_WRITE(sc, AR71XX_SPI_IO_CTRL, SPI_IO_CTRL_CSMASK);

	/*
	 * .. and ensure the write has gone out before continuing.
	 */
	SPI_BARRIER_WRITE(sc);

	device_add_child(dev, "spibus", -1);
	return (bus_generic_attach(dev));
}

static void
ar71xx_spi_chip_activate(struct ar71xx_spi_softc *sc, int cs)
{
	uint32_t ioctrl = SPI_IO_CTRL_CSMASK;
	/*
	 * Put respective CSx to low
	 */
	ioctrl &= ~(SPI_IO_CTRL_CS0 << cs);

	/*
	 * Make sure any other writes have gone out to the
	 * device before changing the chip select line;
	 * then ensure that it has made it out to the device
	 * before continuing.
	 */
	SPI_BARRIER_WRITE(sc);
	SPI_WRITE(sc, AR71XX_SPI_IO_CTRL, ioctrl);
	SPI_BARRIER_WRITE(sc);
}

static void
ar71xx_spi_chip_deactivate(struct ar71xx_spi_softc *sc, int cs)
{
	/*
	 * Put all CSx to high
	 */
	SPI_WRITE(sc, AR71XX_SPI_IO_CTRL, SPI_IO_CTRL_CSMASK);
}

static uint8_t
ar71xx_spi_txrx(struct ar71xx_spi_softc *sc, int cs, uint8_t data)
{
	int bit;
	/* CS0 */
	uint32_t ioctrl = SPI_IO_CTRL_CSMASK;
	/*
	 * low-level for selected CS
	 */
	ioctrl &= ~(SPI_IO_CTRL_CS0 << cs);

	uint32_t iod, rds;
	for (bit = 7; bit >=0; bit--) {
		if (data & (1 << bit))
			iod = ioctrl | SPI_IO_CTRL_DO;
		else
			iod = ioctrl & ~SPI_IO_CTRL_DO;
		SPI_BARRIER_WRITE(sc);
		SPI_WRITE(sc, AR71XX_SPI_IO_CTRL, iod);
		SPI_BARRIER_WRITE(sc);
		SPI_WRITE(sc, AR71XX_SPI_IO_CTRL, iod | SPI_IO_CTRL_CLK);
	}

	/*
	 * Provide falling edge for connected device by clear clock bit.
	 */
	SPI_BARRIER_WRITE(sc);
	SPI_WRITE(sc, AR71XX_SPI_IO_CTRL, iod);
	SPI_BARRIER_WRITE(sc);
	rds = SPI_READ(sc, AR71XX_SPI_RDS);

	return (rds & 0xff);
}

static int
ar71xx_spi_transfer(device_t dev, device_t child, struct spi_command *cmd)
{
	struct ar71xx_spi_softc *sc;
	uint32_t cs;
	uint8_t *buf_in, *buf_out;
	int i;

	sc = device_get_softc(dev);

	spibus_get_cs(child, &cs);

	cs &= ~SPIBUS_CS_HIGH;

	ar71xx_spi_chip_activate(sc, cs);

	KASSERT(cmd->tx_cmd_sz == cmd->rx_cmd_sz, 
	    ("TX/RX command sizes should be equal"));
	KASSERT(cmd->tx_data_sz == cmd->rx_data_sz, 
	    ("TX/RX data sizes should be equal"));

	/*
	 * Transfer command
	 */
	buf_out = (uint8_t *)cmd->tx_cmd;
	buf_in = (uint8_t *)cmd->rx_cmd;
	for (i = 0; i < cmd->tx_cmd_sz; i++)
		buf_in[i] = ar71xx_spi_txrx(sc, cs, buf_out[i]);

	/*
	 * Receive/transmit data (depends on  command)
	 */
	buf_out = (uint8_t *)cmd->tx_data;
	buf_in = (uint8_t *)cmd->rx_data;
	for (i = 0; i < cmd->tx_data_sz; i++)
		buf_in[i] = ar71xx_spi_txrx(sc, cs, buf_out[i]);

	ar71xx_spi_chip_deactivate(sc, cs);

	return (0);
}

static int
ar71xx_spi_detach(device_t dev)
{
	struct ar71xx_spi_softc *sc = device_get_softc(dev);

	/*
	 * Ensure any other writes to the device are finished
	 * before we tear down the SPI device.
	 */
	SPI_BARRIER_WRITE(sc);

	/*
	 * Restore the control register; ensure it has hit the
	 * hardware before continuing.
	 */
	SPI_WRITE(sc, AR71XX_SPI_CTRL, sc->sc_reg_ctrl);
	SPI_BARRIER_WRITE(sc);

	/*
	 * And now, put the flash back into mapped IO mode and
	 * ensure _that_ has completed before we finish up.
	 */
	SPI_WRITE(sc, AR71XX_SPI_FS, 0);
	SPI_BARRIER_WRITE(sc);

	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);

	return (0);
}

static device_method_t ar71xx_spi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ar71xx_spi_probe),
	DEVMETHOD(device_attach,	ar71xx_spi_attach),
	DEVMETHOD(device_detach,	ar71xx_spi_detach),

	DEVMETHOD(spibus_transfer,	ar71xx_spi_transfer),

	{0, 0}
};

static driver_t ar71xx_spi_driver = {
	"spi",
	ar71xx_spi_methods,
	sizeof(struct ar71xx_spi_softc),
};

static devclass_t ar71xx_spi_devclass;

DRIVER_MODULE(ar71xx_spi, nexus, ar71xx_spi_driver, ar71xx_spi_devclass, 0, 0);
