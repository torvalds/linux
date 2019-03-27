/*-
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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

/*
 * Xilinx AXI_QUAD_SPI
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>

#include <dev/spibus/spi.h>
#include <dev/spibus/spibusvar.h>

#include "spibus_if.h"

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#define	READ4(_sc, _reg)	\
	bus_space_read_4(_sc->bst, _sc->bsh, _reg)
#define	WRITE4(_sc, _reg, _val)	\
	bus_space_write_4(_sc->bst, _sc->bsh, _reg, _val)

#define	SPI_SRR		0x40		/* Software reset register */
#define	 SRR_RESET	0x0A		/* The only reset value */
#define	SPI_CR		0x60		/* Control register */
#define	 CR_LSB_FIRST	(1 << 9)	/* LSB first */
#define	 CR_MASTER_TI	(1 << 8)	/* Master Transaction Inhibit */
#define	 CR_MSS		(1 << 7)	/* Manual Slave Select */
#define	 CR_RST_RX	(1 << 6)	/* RX FIFO Reset */
#define	 CR_RST_TX	(1 << 5)	/* TX FIFO Reset */
#define	 CR_CPHA	(1 << 4)	/* Clock phase */
#define	 CR_CPOL	(1 << 3)	/* Clock polarity */
#define	 CR_MASTER	(1 << 2)	/* Master (SPI master mode) */
#define	 CR_SPE		(1 << 1)	/* SPI system enable */
#define	 CR_LOOP	(1 << 0)	/* Local loopback mode */
#define	SPI_SR		0x64		/* Status register */
#define	 SR_TX_FULL	(1 << 3)	/* Transmit full */
#define	 SR_TX_EMPTY	(1 << 2)	/* Transmit empty */
#define	 SR_RX_FULL	(1 << 1)	/* Receive full */
#define	 SR_RX_EMPTY	(1 << 0)	/* Receive empty */
#define	SPI_DTR		0x68		/* Data transmit register */
#define	SPI_DRR		0x6C		/* Data receive register */
#define	SPI_SSR		0x70		/* Slave select register */
#define	SPI_TFOR	0x74		/* Transmit FIFO Occupancy Register */
#define	SPI_RFOR	0x78		/* Receive FIFO Occupancy Register */
#define	SPI_DGIER	0x1C		/* Device global interrupt enable register */
#define	SPI_IPISR	0x20		/* IP interrupt status register */
#define	SPI_IPIER	0x28		/* IP interrupt enable register */

struct spi_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	void			*ih;
};

static struct resource_spec spi_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
spi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "xlnx,xps-spi-3.2"))
		return (ENXIO);

	device_set_desc(dev, "Xilinx Quad SPI");
	return (BUS_PROBE_DEFAULT);
}

static int
spi_attach(device_t dev)
{
	struct spi_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, spi_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	/* Reset */
	WRITE4(sc, SPI_SRR, SRR_RESET);

	DELAY(1000);

	reg = (CR_MASTER | CR_MSS | CR_RST_RX | CR_RST_TX);
	WRITE4(sc, SPI_CR, reg);
	WRITE4(sc, SPI_DGIER, 0);	/* Disable interrupts */

	reg = (CR_MASTER | CR_MSS | CR_SPE);
	WRITE4(sc, SPI_CR, reg);

	device_add_child(dev, "spibus", 0);
	return (bus_generic_attach(dev));
}

static int
spi_txrx(struct spi_softc *sc, uint8_t *out_buf,
    uint8_t *in_buf, int bufsz, int cs)
{
	uint32_t data;
	uint32_t i;

	for (i = 0; i < bufsz; i++) {
		WRITE4(sc, SPI_DTR, out_buf[i]);

		while(!(READ4(sc, SPI_SR) & SR_TX_EMPTY))
			continue;

		data = READ4(sc, SPI_DRR);
		if (in_buf)
			in_buf[i] = (data & 0xff);
	}

	return (0);
}

static int
spi_transfer(device_t dev, device_t child, struct spi_command *cmd)
{
	struct spi_softc *sc;
	uint32_t reg;
	uint32_t cs;

	sc = device_get_softc(dev);

	KASSERT(cmd->tx_cmd_sz == cmd->rx_cmd_sz,
	    ("%s: TX/RX command sizes should be equal", __func__));
	KASSERT(cmd->tx_data_sz == cmd->rx_data_sz,
	    ("%s: TX/RX data sizes should be equal", __func__));

	/* get the proper chip select */
	spibus_get_cs(child, &cs);

	cs &= ~SPIBUS_CS_HIGH;

	/* Assert CS */
	reg = READ4(sc, SPI_SSR);
	reg &= ~(1 << cs);
	WRITE4(sc, SPI_SSR, reg);

	/* Command */
	spi_txrx(sc, cmd->tx_cmd, cmd->rx_cmd, cmd->tx_cmd_sz, cs);

	/* Data */
	spi_txrx(sc, cmd->tx_data, cmd->rx_data, cmd->tx_data_sz, cs);

	/* Deassert CS */
	reg = READ4(sc, SPI_SSR);
	reg |= (1 << cs);
	WRITE4(sc, SPI_SSR, reg);

	return (0);
}

static device_method_t spi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		spi_probe),
	DEVMETHOD(device_attach,	spi_attach),

	/* SPI interface */
	DEVMETHOD(spibus_transfer,	spi_transfer),
	DEVMETHOD_END
};

static driver_t spi_driver = {
	"spi",
	spi_methods,
	sizeof(struct spi_softc),
};

static devclass_t spi_devclass;

DRIVER_MODULE(spi, simplebus, spi_driver, spi_devclass, 0, 0);
