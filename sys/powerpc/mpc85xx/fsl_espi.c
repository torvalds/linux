/*-
 * Copyright (c) 2017 Justin Hibbits <jhibbits@FreeBSD.org>
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
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>

#include <machine/bus.h>

#include <dev/spibus/spi.h>
#include <dev/spibus/spibusvar.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <powerpc/mpc85xx/mpc85xx.h>

#include "spibus_if.h"

/* TODO:
 *
 * Optimize FIFO reads and writes to do word-at-a-time instead of byte-at-a-time
 */
#define	ESPI_SPMODE	0x0
#define	  ESPI_SPMODE_EN	  0x80000000
#define	  ESPI_SPMODE_LOOP	  0x40000000
#define	  ESPI_SPMODE_HO_ADJ_M	  0x00070000
#define	  ESPI_SPMODE_TXTHR_M	  0x00003f00
#define	  ESPI_SPMODE_TXTHR_S	  8
#define	  ESPI_SPMODE_RXTHR_M	  0x0000001f
#define	  ESPI_SPMODE_RXTHR_S	  0
#define	ESPI_SPIE	0x4
#define	  ESPI_SPIE_RXCNT_M	  0x3f000000
#define	  ESPI_SPIE_RXCNT_S	  24
#define	  ESPI_SPIE_TXCNT_M	  0x003f0000
#define	  ESPI_SPIE_TXCNT_S	  16
#define	  ESPI_SPIE_TXE		  0x00008000
#define	  ESPI_SPIE_DON		  0x00004000
#define	  ESPI_SPIE_RXT		  0x00002000
#define	  ESPI_SPIE_RXF		  0x00001000
#define	  ESPI_SPIE_TXT		  0x00000800
#define	  ESPI_SPIE_RNE		  0x00000200
#define	  ESPI_SPIE_TNF		  0x00000100
#define	ESPI_SPIM	0x8
#define	ESPI_SPCOM	0xc
#define	  ESPI_SPCOM_CS_M	  0xc0000000
#define	  ESPI_SPCOM_CS_S	  30
#define	  ESPI_SPCOM_RXDELAY	  0x20000000
#define	  ESPI_SPCOM_DO		  0x10000000
#define	  ESPI_SPCOM_TO		  0x08000000
#define	  ESPI_SPCOM_HLD	  0x04000000
#define	  ESPI_SPCOM_RXSKIP_M	  0x00ff0000
#define	  ESPI_SPCOM_TRANLEN_M	  0x0000ffff
#define	ESPI_SPITF	0x10
#define	ESPI_SPIRF	0x14
#define	ESPI_SPMODE0	0x20
#define	ESPI_SPMODE1	0x24
#define	ESPI_SPMODE2	0x28
#define	ESPI_SPMODE3	0x2c
#define	  ESPI_CSMODE_CI	  0x80000000
#define	  ESPI_CSMODE_CP	  0x40000000
#define	  ESPI_CSMODE_REV	  0x20000000
#define	  ESPI_CSMODE_DIV16	  0x10000000
#define	  ESPI_CSMODE_PM_M	  0x0f000000
#define	  ESPI_CSMODE_PM_S	  24
#define	  ESPI_CSMODE_ODD	  0x00800000
#define	  ESPI_CSMODE_POL	  0x00100000
#define	  ESPI_CSMODE_LEN_M	  0x000f0000
#define	    ESPI_CSMODE_LEN(x)	    (x << 16)
#define	  ESPI_CSMODE_CSBEF_M	  0x0000f000
#define	  ESPI_CSMODE_CSAFT_M	  0x00000f00
#define	  ESPI_CSMODE_CSCG_M	  0x000000f8
#define	    ESPI_CSMODE_CSCG(x)	    (x << 3)
#define	ESPI_CSMODE(n)		(ESPI_SPMODE0 + n * 4)

#define	FSL_ESPI_WRITE(sc,off,val)	bus_write_4(sc->sc_mem_res, off, val)
#define	FSL_ESPI_READ(sc,off)		bus_read_4(sc->sc_mem_res, off)
#define	FSL_ESPI_WRITE_FIFO(sc,off,val)	bus_write_1(sc->sc_mem_res, off, val)
#define	FSL_ESPI_READ_FIFO(sc,off)	bus_read_1(sc->sc_mem_res, off)

#define FSL_ESPI_LOCK(_sc)			\
    mtx_lock(&(_sc)->sc_mtx)
#define FSL_ESPI_UNLOCK(_sc)			\
    mtx_unlock(&(_sc)->sc_mtx)

struct fsl_espi_softc
{
	device_t		sc_dev;
	struct resource		*sc_mem_res;
	struct resource		*sc_irq_res;
	struct mtx		sc_mtx;
	int			sc_num_cs;
	struct spi_command	*sc_cmd;
	uint32_t		sc_len;
	uint32_t		sc_read;
	uint32_t		sc_flags;
#define	  FSL_ESPI_BUSY		  0x00000001
	uint32_t		sc_written;
	void *			sc_intrhand;
};

static void fsl_espi_intr(void *);

static int
fsl_espi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mpc8536-espi"))
		return (ENXIO);

	device_set_desc(dev, "Freescale eSPI controller");

	return (BUS_PROBE_DEFAULT);
}

static int
fsl_espi_attach(device_t dev)
{
	struct fsl_espi_softc *sc;
	int rid;
	phandle_t node;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	node = ofw_bus_get_node(dev);

	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->sc_mem_res) {
		device_printf(dev, "cannot allocate memory resource\n");
		return (ENXIO);
	}

	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (!sc->sc_irq_res) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		device_printf(dev, "cannot allocate interrupt\n");
		return (ENXIO);
	}

	/* Hook up our interrupt handler. */
	if (bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, fsl_espi_intr, sc, &sc->sc_intrhand)) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		device_printf(dev, "cannot setup the interrupt handler\n");
		return (ENXIO);
	}
	if (OF_getencprop(node, "fsl,espi-num-chipselects",
	    &sc->sc_num_cs, sizeof(sc->sc_num_cs)) < 0 )
		sc->sc_num_cs = 4;

	mtx_init(&sc->sc_mtx, "fsl_espi", NULL, MTX_DEF);

	/* Enable the SPI controller.  */
	FSL_ESPI_WRITE(sc, ESPI_SPMODE, ESPI_SPMODE_EN | 
	    (16 << ESPI_SPMODE_TXTHR_S) | (15 << ESPI_SPMODE_RXTHR_S));

	/* Disable all interrupts until we start transfers  */
	FSL_ESPI_WRITE(sc, ESPI_SPIM, 0);

	device_add_child(dev, "spibus", -1);

	return (bus_generic_attach(dev));
}

static int
fsl_espi_detach(device_t dev)
{
	struct fsl_espi_softc *sc;

	bus_generic_detach(dev);

	sc = device_get_softc(dev);
	FSL_ESPI_WRITE(sc, ESPI_SPMODE, 0);

	sc = device_get_softc(dev);
	mtx_destroy(&sc->sc_mtx);
	if (sc->sc_intrhand)
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_intrhand);
	if (sc->sc_irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);

	return (0);
}

static void
fsl_espi_fill_fifo(struct fsl_espi_softc *sc)
{
	struct spi_command *cmd;
	uint32_t spier, written;
	uint8_t *data;

	cmd = sc->sc_cmd;
	spier = FSL_ESPI_READ(sc, ESPI_SPIE);
	while (sc->sc_written < sc->sc_len &&
	    (spier & ESPI_SPIE_TNF)) {
		data = (uint8_t *)cmd->tx_cmd;
		written = sc->sc_written++;
		if (written >= cmd->tx_cmd_sz) {
			data = (uint8_t *)cmd->tx_data;
			written -= cmd->tx_cmd_sz;
		}
		FSL_ESPI_WRITE_FIFO(sc, ESPI_SPITF, data[written]);
		spier = FSL_ESPI_READ(sc, ESPI_SPIE);
	}
}

static void
fsl_espi_drain_fifo(struct fsl_espi_softc *sc)
{
	struct spi_command *cmd;
	uint32_t spier, read;
	uint8_t *data;
	uint8_t r;

	cmd = sc->sc_cmd;
	spier = FSL_ESPI_READ(sc, ESPI_SPIE);
	while (sc->sc_read < sc->sc_len && (spier & ESPI_SPIE_RNE)) {
		data = (uint8_t *)cmd->rx_cmd;
		read = sc->sc_read++;
		if (read >= cmd->rx_cmd_sz) {
			data = (uint8_t *)cmd->rx_data;
			read -= cmd->rx_cmd_sz;
		}
		r = FSL_ESPI_READ_FIFO(sc, ESPI_SPIRF);
		data[read] = r;
		spier = FSL_ESPI_READ(sc, ESPI_SPIE);
	}
}

static void
fsl_espi_intr(void *arg)
{
	struct fsl_espi_softc *sc;
	uint32_t spie;

	sc = (struct fsl_espi_softc *)arg;
	FSL_ESPI_LOCK(sc);

	/* Filter stray interrupts. */
	if ((sc->sc_flags & FSL_ESPI_BUSY) == 0) {
		FSL_ESPI_UNLOCK(sc);
		return;
	}
	spie = FSL_ESPI_READ(sc, ESPI_SPIE);
	FSL_ESPI_WRITE(sc, ESPI_SPIE, spie);

	/* TX - Fill up the FIFO. */
	fsl_espi_fill_fifo(sc);

	/* RX - Drain the FIFO. */
	fsl_espi_drain_fifo(sc);

	/* Check for end of transfer. */
	if (spie & ESPI_SPIE_DON)
		wakeup(sc->sc_dev);

	FSL_ESPI_UNLOCK(sc);
}

static int
fsl_espi_transfer(device_t dev, device_t child, struct spi_command *cmd)
{
	struct fsl_espi_softc *sc;
	u_long plat_clk;
	uint32_t csmode, spi_clk, spi_mode;
	int cs, err, pm;

	sc = device_get_softc(dev);

	KASSERT(cmd->tx_cmd_sz == cmd->rx_cmd_sz, 
	    ("TX/RX command sizes should be equal"));
	KASSERT(cmd->tx_data_sz == cmd->rx_data_sz, 
	    ("TX/RX data sizes should be equal"));

	/* Restrict transmit length to command max length */
	if (cmd->tx_cmd_sz + cmd->tx_data_sz > ESPI_SPCOM_TRANLEN_M + 1) {
		return (EINVAL);
	}

	/* Get the proper chip select for this child. */
	spibus_get_cs(child, &cs);
	if (cs < 0 || cs > sc->sc_num_cs) {
		device_printf(dev,
		    "Invalid chip select %d requested by %s\n", cs,
		    device_get_nameunit(child));
		return (EINVAL);
	}
	spibus_get_clock(child, &spi_clk);
	spibus_get_mode(child, &spi_mode);

	FSL_ESPI_LOCK(sc);

	/* If the controller is in use wait until it is available. */
	while (sc->sc_flags & FSL_ESPI_BUSY)
		mtx_sleep(dev, &sc->sc_mtx, 0, "fsl_espi", 0);

	/* Now we have control over SPI controller. */
	sc->sc_flags = FSL_ESPI_BUSY;

	/* Save a pointer to the SPI command. */
	sc->sc_cmd = cmd;
	sc->sc_read = 0;
	sc->sc_written = 0;
	sc->sc_len = cmd->tx_cmd_sz + cmd->tx_data_sz;

	plat_clk = mpc85xx_get_system_clock();
	spi_clk = max(spi_clk, plat_clk / (16 * 16));
	if (plat_clk == 0) {
		device_printf(dev,
		    "unable to get platform clock, giving up.\n");
		return (EINVAL);
	}
	csmode = 0;
	if (plat_clk > spi_clk * 16 * 2) {
		csmode |= ESPI_CSMODE_DIV16;
		plat_clk /= 16;
	}
	pm = howmany(plat_clk, spi_clk * 2) - 1;
	if (pm < 0)
		pm = 1;
	if (pm > 15)
		pm = 15;

	csmode |= (pm << ESPI_CSMODE_PM_S);
	csmode |= ESPI_CSMODE_REV;
	if (spi_mode == SPIBUS_MODE_CPOL || spi_mode == SPIBUS_MODE_CPOL_CPHA)
		csmode |= ESPI_CSMODE_CI;
	if (spi_mode == SPIBUS_MODE_CPHA || spi_mode == SPIBUS_MODE_CPOL_CPHA)
		csmode |= ESPI_CSMODE_CP;
	if (!(cs & SPIBUS_CS_HIGH))
		csmode |= ESPI_CSMODE_POL;
	csmode |= ESPI_CSMODE_LEN(7);/* Only deal with 8-bit characters. */
	csmode |= ESPI_CSMODE_CSCG(1); /* XXX: Make this configurable? */
	/* Configure transaction */
	FSL_ESPI_WRITE(sc, ESPI_SPCOM, (cs << ESPI_SPCOM_CS_S) | (sc->sc_len - 1));
	FSL_ESPI_WRITE(sc, ESPI_CSMODE(cs), csmode);
	/* Enable interrupts we need. */
	FSL_ESPI_WRITE(sc, ESPI_SPIM,
	    ESPI_SPIE_TXE | ESPI_SPIE_DON | ESPI_SPIE_RXF);

	/* Wait for the transaction to complete. */
	err = mtx_sleep(dev, &sc->sc_mtx, 0, "fsl_espi", hz * 2);
	FSL_ESPI_WRITE(sc, ESPI_SPIM, 0);

	/* Release the controller and wakeup the next thread waiting for it. */
	sc->sc_flags = 0;
	wakeup_one(dev);
	FSL_ESPI_UNLOCK(sc);

	/*
	 * Check for transfer timeout.  The SPI controller doesn't
	 * return errors.
	 */
	if (err == EWOULDBLOCK) {
		device_printf(sc->sc_dev, "SPI error\n");
		err = EIO;
	}

	return (err);
}

static phandle_t
fsl_espi_get_node(device_t bus, device_t dev)
{

	/* We only have one child, the SPI bus, which needs our own node. */
	return (ofw_bus_get_node(bus));
}

static device_method_t fsl_espi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fsl_espi_probe),
	DEVMETHOD(device_attach,	fsl_espi_attach),
	DEVMETHOD(device_detach,	fsl_espi_detach),

	/* SPI interface */
	DEVMETHOD(spibus_transfer,	fsl_espi_transfer),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	fsl_espi_get_node),

	DEVMETHOD_END
};

static devclass_t fsl_espi_devclass;

static driver_t fsl_espi_driver = {
	"spi",
	fsl_espi_methods,
	sizeof(struct fsl_espi_softc),
};

DRIVER_MODULE(fsl_espi, simplebus, fsl_espi_driver, fsl_espi_devclass, 0, 0);
