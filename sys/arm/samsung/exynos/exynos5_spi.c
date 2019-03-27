/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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

/*
 * Exynos 5 Serial Peripheral Interface (SPI)
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

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/samsung/exynos/exynos5_common.h>

#define	CH_CFG		0x00		/* SPI configuration */
#define	 SW_RST		(1 << 5)	/* Reset */
#define	 RX_CH_ON	(1 << 1)	/* SPI Rx Channel On */
#define	 TX_CH_ON	(1 << 0)	/* SPI Tx Channel On */
#define	MODE_CFG	0x08		/* FIFO control */
#define	CS_REG		0x0C		/* slave selection control */
#define	 NSSOUT		(1 << 0)
#define	SPI_INT_EN	0x10		/* interrupt enable */
#define	SPI_STATUS	0x14		/* SPI status */
#define	 TX_FIFO_LVL_S	6
#define	 TX_FIFO_LVL_M	0x1ff
#define	 RX_FIFO_LVL_S	15
#define	 RX_FIFO_LVL_M	0x1ff
#define	SPI_TX_DATA	0x18		/* Tx data */
#define	SPI_RX_DATA	0x1C		/* Rx data */
#define	PACKET_CNT_REG	0x20		/* packet count */
#define	PENDING_CLR_REG	0x24		/* interrupt pending clear */
#define	SWAP_CFG	0x28		/* swap configuration */
#define	FB_CLK_SEL	0x2C		/* feedback clock selection */
#define	 FB_CLK_180	0x2		/* 180 degree phase lagging */

struct spi_softc {
	struct resource		*res[2];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
};

struct spi_softc *spi_sc;

static struct resource_spec spi_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
spi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "samsung,exynos5-spi"))
		return (ENXIO);

	device_set_desc(dev, "Exynos 5 Serial Peripheral Interface (SPI)");
	return (BUS_PROBE_DEFAULT);
}

static int
spi_attach(device_t dev)
{
	struct spi_softc *sc;
	int reg;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, spi_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	spi_sc = sc;

	WRITE4(sc, FB_CLK_SEL, FB_CLK_180);

	reg = READ4(sc, CH_CFG);
	reg |= (RX_CH_ON | TX_CH_ON);
	WRITE4(sc, CH_CFG, reg);

	device_add_child(dev, "spibus", 0);
	return (bus_generic_attach(dev));
}

static int
spi_txrx(struct spi_softc *sc, uint8_t *out_buf,
    uint8_t *in_buf, int bufsz, int cs)
{
	uint32_t reg;
	uint32_t i;

	if (bufsz == 0) {
		/* Nothing to transfer */
		return (0);
	}

	/* Reset registers */
	reg = READ4(sc, CH_CFG);
	reg |= SW_RST;
	WRITE4(sc, CH_CFG, reg);
	reg &= ~SW_RST;
	WRITE4(sc, CH_CFG, reg);

	/* Assert CS */
	reg = READ4(sc, CS_REG);
	reg &= ~NSSOUT;
	WRITE4(sc, CS_REG, reg);

	for (i = 0; i < bufsz; i++) {

		/* TODO: Implement FIFO operation */

		/* Wait all the data shifted out */
		while (READ4(sc, SPI_STATUS) & \
		    (TX_FIFO_LVL_M << TX_FIFO_LVL_S))
			continue;

		WRITE1(sc, SPI_TX_DATA, out_buf[i]);

		/* Wait until no data available */
		while ((READ4(sc, SPI_STATUS) & \
			(RX_FIFO_LVL_M << RX_FIFO_LVL_S)) == 0)
			continue;

		in_buf[i] = READ1(sc, SPI_RX_DATA);
	}

	/* Deassert CS */
	reg = READ4(sc, CS_REG);
	reg |= NSSOUT;
	WRITE4(sc, CS_REG, reg);

	return (0);
}

static int
spi_transfer(device_t dev, device_t child, struct spi_command *cmd)
{
	struct spi_softc *sc;
	uint32_t cs;

	sc = device_get_softc(dev);

	KASSERT(cmd->tx_cmd_sz == cmd->rx_cmd_sz,
	    ("%s: TX/RX command sizes should be equal", __func__));
	KASSERT(cmd->tx_data_sz == cmd->rx_data_sz,
	    ("%s: TX/RX data sizes should be equal", __func__));

	/* get the proper chip select */
	spibus_get_cs(child, &cs);

	cs &= ~SPIBUS_CS_HIGH;

	/* Command */
	spi_txrx(sc, cmd->tx_cmd, cmd->rx_cmd, cmd->tx_cmd_sz, cs);

	/* Data */
	spi_txrx(sc, cmd->tx_data, cmd->rx_data, cmd->tx_data_sz, cs);

	return (0);
}

static device_method_t spi_methods[] = {
	DEVMETHOD(device_probe,		spi_probe),
	DEVMETHOD(device_attach,	spi_attach),

	/* SPI interface */
	DEVMETHOD(spibus_transfer,	spi_transfer),

	{ 0, 0 }
};

static driver_t spi_driver = {
	"spi",
	spi_methods,
	sizeof(struct spi_softc),
};

static devclass_t spi_devclass;

DRIVER_MODULE(spi, simplebus, spi_driver, spi_devclass, 0, 0);
