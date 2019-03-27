/*-
 * Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/resource.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/spibus/spi.h>
#include <dev/spibus/spibusvar.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>

#include "spibus_if.h"

#define	AW_SPI_GCR		0x04		/* Global Control Register */
#define	 AW_SPI_GCR_EN		(1 << 0)	/* ENable */
#define	 AW_SPI_GCR_MODE_MASTER	(1 << 1)	/* 1 = Master, 0 = Slave */
#define	 AW_SPI_GCR_TP_EN	(1 << 7)	/* 1 = Stop transmit when FIFO is full */
#define	 AW_SPI_GCR_SRST	(1 << 31)	/* Soft Reset */

#define	AW_SPI_TCR		0x08		/* Transfer Control register */
#define	 AW_SPI_TCR_XCH		(1 << 31)	/* Initiate transfer */
#define	 AW_SPI_TCR_SDDM	(1 << 14)	/* Sending Delay Data Mode */
#define	 AW_SPI_TCR_SDM		(1 << 13)	/* Master Sample Data Mode */
#define	 AW_SPI_TCR_FBS		(1 << 12)	/* First Transmit Bit Select (1 == LSB) */
#define	 AW_SPI_TCR_SDC		(1 << 11)	/* Master Sample Data Control */
#define	 AW_SPI_TCR_RPSM	(1 << 10)	/* Rapid Mode Select */
#define	 AW_SPI_TCR_DDB		(1 << 9)	/* Dummy Burst Type */
#define	 AW_SPI_TCR_SSSEL_MASK	0x30		/* Chip select */
#define	 AW_SPI_TCR_SSSEL_SHIFT	4
#define	 AW_SPI_TCR_SS_LEVEL	(1 << 7)	/* 1 == CS High */
#define	 AW_SPI_TCR_SS_OWNER	(1 << 6)	/* 1 == Software controlled */
#define	 AW_SPI_TCR_SPOL	(1 << 2)	/* 1 == Active low */
#define	 AW_SPI_TCR_CPOL	(1 << 1)	/* 1 == Active low */
#define	 AW_SPI_TCR_CPHA	(1 << 0)	/* 1 == Phase 1 */

#define	AW_SPI_IER		0x10		/* Interrupt Control Register */
#define	 AW_SPI_IER_SS		(1 << 13)	/* Chip select went from valid to invalid */
#define	 AW_SPI_IER_TC		(1 << 12)	/* Transfer complete */
#define	 AW_SPI_IER_TF_UDR	(1 << 11)	/* TXFIFO underrun */
#define	 AW_SPI_IER_TF_OVF	(1 << 10)	/* TXFIFO overrun */
#define	 AW_SPI_IER_RF_UDR	(1 << 9)	/* RXFIFO underrun */
#define	 AW_SPI_IER_RF_OVF	(1 << 8)	/* RXFIFO overrun */
#define	 AW_SPI_IER_TF_FULL	(1 << 6)	/* TXFIFO Full */
#define	 AW_SPI_IER_TF_EMP	(1 << 5)	/* TXFIFO Empty */
#define	 AW_SPI_IER_TF_ERQ	(1 << 4)	/* TXFIFO Empty Request */
#define	 AW_SPI_IER_RF_FULL	(1 << 2)	/* RXFIFO Full */
#define	 AW_SPI_IER_RF_EMP	(1 << 1)	/* RXFIFO Empty */
#define	 AW_SPI_IER_RF_ERQ	(1 << 0)	/* RXFIFO Empty Request */

#define	AW_SPI_ISR		0x14		/* Interrupt Status Register */

#define	AW_SPI_FCR			0x18		/* FIFO Control Register */
#define	 AW_SPI_FCR_TX_RST		(1 << 31)	/* Reset TX FIFO */
#define	 AW_SPI_FCR_TX_TRIG_MASK	0xFF0000	/* TX FIFO Trigger level */
#define	 AW_SPI_FCR_TX_TRIG_SHIFT	16
#define	 AW_SPI_FCR_RX_RST	(1 << 15)		/* Reset RX FIFO */
#define	 AW_SPI_FCR_RX_TRIG_MASK	0xFF		/* RX FIFO Trigger level */
#define	 AW_SPI_FCR_RX_TRIG_SHIFT	0

#define	AW_SPI_FSR	0x1C			/* FIFO Status Register */
#define	 AW_SPI_FSR_TB_WR		(1 << 31)
#define	 AW_SPI_FSR_TB_CNT_MASK		0x70000000
#define	 AW_SPI_FSR_TB_CNT_SHIFT	28
#define	 AW_SPI_FSR_TF_CNT_MASK		0xFF0000
#define	 AW_SPI_FSR_TF_CNT_SHIFT	16
#define	 AW_SPI_FSR_RB_WR		(1 << 15)
#define	 AW_SPI_FSR_RB_CNT_MASK		0x7000
#define	 AW_SPI_FSR_RB_CNT_SHIFT	12
#define	 AW_SPI_FSR_RF_CNT_MASK		0xFF
#define	 AW_SPI_FSR_RF_CNT_SHIFT	0

#define	AW_SPI_WCR	0x20	/* Wait Clock Counter Register */

#define	AW_SPI_CCR	0x24		/* Clock Rate Control Register */
#define	 AW_SPI_CCR_DRS	(1 << 12)	/* Clock divider select */
#define	 AW_SPI_CCR_CDR1_MASK	0xF00
#define	 AW_SPI_CCR_CDR1_SHIFT	8
#define	 AW_SPI_CCR_CDR2_MASK	0xFF
#define	 AW_SPI_CCR_CDR2_SHIFT	0

#define	AW_SPI_MBC	0x30	/* Burst Counter Register */
#define	AW_SPI_MTC	0x34	/* Transmit Counter Register */
#define	AW_SPI_BCC	0x38	/* Burst Control Register */
#define	AW_SPI_MDMA_CTL	0x88	/* Normal DMA Control Register */
#define	AW_SPI_TXD	0x200	/* TX Data Register */
#define	AW_SPI_RDX	0x300	/* RX Data Register */

#define	AW_SPI_MAX_CS		4
#define	AW_SPI_FIFO_SIZE	64

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun8i-h3-spi",		1 },
	{ NULL,					0 }
};

static struct resource_spec aw_spi_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

struct aw_spi_softc {
	device_t	dev;
	device_t	spibus;
	struct resource	*res[2];
	struct mtx	mtx;
	clk_t		clk_ahb;
	clk_t		clk_mod;
	uint64_t	mod_freq;
	hwreset_t	rst_ahb;
	void *		intrhand;
	int		transfer;

	uint8_t		*rxbuf;
	uint32_t	rxcnt;
	uint8_t		*txbuf;
	uint32_t	txcnt;
	uint32_t	txlen;
	uint32_t	rxlen;
};

#define	AW_SPI_LOCK(sc)			mtx_lock(&(sc)->mtx)
#define	AW_SPI_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	AW_SPI_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->mtx, MA_OWNED)
#define	AW_SPI_READ_1(sc, reg)		bus_read_1((sc)->res[0], (reg))
#define	AW_SPI_WRITE_1(sc, reg, val)	bus_write_1((sc)->res[0], (reg), (val))
#define	AW_SPI_READ_4(sc, reg)		bus_read_4((sc)->res[0], (reg))
#define	AW_SPI_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], (reg), (val))

static int aw_spi_probe(device_t dev);
static int aw_spi_attach(device_t dev);
static int aw_spi_detach(device_t dev);
static void aw_spi_intr(void *arg);

static int
aw_spi_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Allwinner SPI");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_spi_attach(device_t dev)
{
	struct aw_spi_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	if (bus_alloc_resources(dev, aw_spi_spec, sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		error = ENXIO;
		goto fail;
	}

	if (bus_setup_intr(dev, sc->res[1],
	    INTR_TYPE_MISC | INTR_MPSAFE, NULL, aw_spi_intr, sc,
	    &sc->intrhand)) {
		bus_release_resources(dev, aw_spi_spec, sc->res);
		device_printf(dev, "cannot setup interrupt handler\n");
		return (ENXIO);
	}

	/* De-assert reset */
	if (hwreset_get_by_ofw_idx(dev, 0, 0, &sc->rst_ahb) == 0) {
		error = hwreset_deassert(sc->rst_ahb);
		if (error != 0) {
			device_printf(dev, "cannot de-assert reset\n");
			goto fail;
		}
	}

	/* Activate the module clock. */
	error = clk_get_by_ofw_name(dev, 0, "ahb", &sc->clk_ahb);
	if (error != 0) {
		device_printf(dev, "cannot get ahb clock\n");
		goto fail;
	}
	error = clk_get_by_ofw_name(dev, 0, "mod", &sc->clk_mod);
	if (error != 0) {
		device_printf(dev, "cannot get mod clock\n");
		goto fail;
	}
	error = clk_enable(sc->clk_ahb);
	if (error != 0) {
		device_printf(dev, "cannot enable ahb clock\n");
		goto fail;
	}
	error = clk_enable(sc->clk_mod);
	if (error != 0) {
		device_printf(dev, "cannot enable mod clock\n");
		goto fail;
	}

	sc->spibus = device_add_child(dev, "spibus", -1);

	return (0);

fail:
	aw_spi_detach(dev);
	return (error);
}

static int
aw_spi_detach(device_t dev)
{
	struct aw_spi_softc *sc;

	sc = device_get_softc(dev);

	bus_generic_detach(sc->dev);
	if (sc->spibus != NULL)
		device_delete_child(dev, sc->spibus);

	if (sc->clk_mod != NULL)
		clk_release(sc->clk_mod);
	if (sc->clk_ahb)
		clk_release(sc->clk_ahb);
	if (sc->rst_ahb)
		hwreset_assert(sc->rst_ahb);

	if (sc->intrhand != NULL)
		bus_teardown_intr(sc->dev, sc->res[1], sc->intrhand);

	bus_release_resources(dev, aw_spi_spec, sc->res);
	mtx_destroy(&sc->mtx);

	return (0);
}

static phandle_t
aw_spi_get_node(device_t bus, device_t dev)
{

	return ofw_bus_get_node(bus);
}

static void
aw_spi_setup_mode(struct aw_spi_softc *sc, uint32_t mode)
{
	uint32_t reg;

	/* We only support master mode */
	reg = AW_SPI_READ_4(sc, AW_SPI_GCR);
	reg |= AW_SPI_GCR_MODE_MASTER;
	AW_SPI_WRITE_4(sc, AW_SPI_GCR, reg);

	/* Setup the modes */
	reg = AW_SPI_READ_4(sc, AW_SPI_TCR);
	if (mode & SPIBUS_MODE_CPHA)
		reg |= AW_SPI_TCR_CPHA;
	if (mode & SPIBUS_MODE_CPOL)
		reg |= AW_SPI_TCR_CPOL;

	AW_SPI_WRITE_4(sc, AW_SPI_TCR, reg);
}

static void
aw_spi_setup_cs(struct aw_spi_softc *sc, uint32_t cs, bool low)
{
	uint32_t reg;

	/* Setup CS */
	reg = AW_SPI_READ_4(sc, AW_SPI_TCR);
	reg &= ~(AW_SPI_TCR_SSSEL_MASK);
	reg |= cs << AW_SPI_TCR_SSSEL_SHIFT;
	reg |= AW_SPI_TCR_SS_OWNER;
	if (low)
		reg &= ~(AW_SPI_TCR_SS_LEVEL);
	else
		reg |= AW_SPI_TCR_SS_LEVEL;

	AW_SPI_WRITE_4(sc, AW_SPI_TCR, reg);
}

static uint64_t
aw_spi_clock_test_cdr1(struct aw_spi_softc *sc, uint64_t clock, uint32_t *ccr)
{
	uint64_t cur, best = 0;
	int i, max, best_div;

	max = AW_SPI_CCR_CDR1_MASK >> AW_SPI_CCR_CDR1_SHIFT;
	for (i = 0; i < max; i++) {
		cur = sc->mod_freq / (1 << i);
		if ((clock - cur) < (clock - best)) {
			best = cur;
			best_div = i;
		}
	}

	*ccr = (best_div << AW_SPI_CCR_CDR1_SHIFT);
	return (best);
}

static uint64_t
aw_spi_clock_test_cdr2(struct aw_spi_softc *sc, uint64_t clock, uint32_t *ccr)
{
	uint64_t cur, best = 0;
	int i, max, best_div;

	max = ((AW_SPI_CCR_CDR2_MASK) >> AW_SPI_CCR_CDR2_SHIFT);
	for (i = 0; i < max; i++) {
		cur = sc->mod_freq / (2 * i + 1);
		if ((clock - cur) < (clock - best)) {
			best = cur;
			best_div = i;
		}
	}

	*ccr = AW_SPI_CCR_DRS | (best_div << AW_SPI_CCR_CDR2_SHIFT);
	return (best);
}

static void
aw_spi_setup_clock(struct aw_spi_softc *sc, uint64_t clock)
{
	uint64_t best_ccr1, best_ccr2;
	uint32_t ccr, ccr1, ccr2;

	best_ccr1 = aw_spi_clock_test_cdr1(sc, clock, &ccr1);
	best_ccr2 = aw_spi_clock_test_cdr2(sc, clock, &ccr2);

	if (best_ccr1 == clock) {
		ccr = ccr1;
	} else if (best_ccr2 == clock) {
		ccr = ccr2;
	} else {
		if ((clock - best_ccr1) < (clock - best_ccr2))
			ccr = ccr1;
		else
			ccr = ccr2;
	}

	AW_SPI_WRITE_4(sc, AW_SPI_CCR, ccr);
}

static inline void
aw_spi_fill_txfifo(struct aw_spi_softc *sc)
{
	uint32_t reg, txcnt;
	int i;

	if (sc->txcnt == sc->txlen)
		return;

	reg = AW_SPI_READ_4(sc, AW_SPI_FSR);
	reg &= AW_SPI_FSR_TF_CNT_MASK;
	txcnt = reg >> AW_SPI_FSR_TF_CNT_SHIFT;

	for (i = 0; i < (AW_SPI_FIFO_SIZE - txcnt); i++) {
		AW_SPI_WRITE_1(sc, AW_SPI_TXD, sc->txbuf[sc->txcnt++]);
		if (sc->txcnt == sc->txlen)
			break;
	}

	return;
}

static inline void
aw_spi_read_rxfifo(struct aw_spi_softc *sc)
{
	uint32_t reg;
	uint8_t val;
	int i;

	if (sc->rxcnt == sc->rxlen)
		return;

	reg = AW_SPI_READ_4(sc, AW_SPI_FSR);
	reg = (reg & AW_SPI_FSR_RF_CNT_MASK) >> AW_SPI_FSR_RF_CNT_SHIFT;

	for (i = 0; i < reg; i++) {
		val = AW_SPI_READ_1(sc, AW_SPI_RDX);
		if (sc->rxcnt < sc->rxlen)
			sc->rxbuf[sc->rxcnt++] = val;
	}
}

static void
aw_spi_intr(void *arg)
{
	struct aw_spi_softc *sc;
	uint32_t intr;

	sc = (struct aw_spi_softc *)arg;

	intr = AW_SPI_READ_4(sc, AW_SPI_ISR);

	if (intr & AW_SPI_IER_RF_FULL)
		aw_spi_read_rxfifo(sc);

	if (intr & AW_SPI_IER_TF_EMP) {
		aw_spi_fill_txfifo(sc);
		/* 
		 * If we don't have anything else to write 
		 * disable TXFifo interrupts
		 */
		if (sc->txcnt == sc->txlen)
			AW_SPI_WRITE_4(sc, AW_SPI_IER, AW_SPI_IER_TC |
			    AW_SPI_IER_RF_FULL);
	}

	if (intr & AW_SPI_IER_TC) {
		/* read the rest of the data from the fifo */
		aw_spi_read_rxfifo(sc);

		/* Disable the interrupts */
		AW_SPI_WRITE_4(sc, AW_SPI_IER, 0);
		sc->transfer = 0;
		wakeup(sc);
	}

	/* Clear Interrupts */
	AW_SPI_WRITE_4(sc, AW_SPI_ISR, intr);
}

static int
aw_spi_xfer(struct aw_spi_softc *sc, void *rxbuf, void *txbuf, uint32_t txlen, uint32_t rxlen)
{
	uint32_t reg;
	int error = 0, timeout;

	sc->rxbuf = rxbuf;
	sc->rxcnt = 0;
	sc->txbuf = txbuf;
	sc->txcnt = 0;
	sc->txlen = txlen;
	sc->rxlen = rxlen;

	/* Reset the FIFOs */
	AW_SPI_WRITE_4(sc, AW_SPI_FCR, AW_SPI_FCR_TX_RST | AW_SPI_FCR_RX_RST);

	for (timeout = 1000; timeout > 0; timeout--) {
		reg = AW_SPI_READ_4(sc, AW_SPI_FCR);
		if (reg == 0)
			break;
	}
	if (timeout == 0) {
		device_printf(sc->dev, "Cannot reset the FIFOs\n");
		return (EIO);
	}

	/* Write the counters */
	AW_SPI_WRITE_4(sc, AW_SPI_MBC, txlen);
	AW_SPI_WRITE_4(sc, AW_SPI_MTC, txlen);
	AW_SPI_WRITE_4(sc, AW_SPI_BCC, txlen);

	/* First fill */
	aw_spi_fill_txfifo(sc);

	/* Start transmit */
	reg = AW_SPI_READ_4(sc, AW_SPI_TCR);
	reg |= AW_SPI_TCR_XCH;
	AW_SPI_WRITE_4(sc, AW_SPI_TCR, reg);

	/* 
	 * Enable interrupts for :
	 * Transmit complete
	 * TX Fifo empty
	 * RX Fifo full
	 */
	AW_SPI_WRITE_4(sc, AW_SPI_IER, AW_SPI_IER_TC |
	    AW_SPI_IER_TF_EMP | AW_SPI_IER_RF_FULL);

	sc->transfer = 1;

	while (error == 0 && sc->transfer != 0)
		error = msleep(sc, &sc->mtx, 0, "aw_spi", 10 * hz);

	return (0);
}

static int
aw_spi_transfer(device_t dev, device_t child, struct spi_command *cmd)
{
	struct aw_spi_softc *sc;
	uint32_t cs, mode, clock, reg;
	int err = 0;

	sc = device_get_softc(dev);

	spibus_get_cs(child, &cs);
	spibus_get_clock(child, &clock);
	spibus_get_mode(child, &mode);

	/* The minimum divider is 2 so set the clock at twice the needed speed */
	clk_set_freq(sc->clk_mod, 2 * clock, CLK_SET_ROUND_DOWN);
	clk_get_freq(sc->clk_mod, &sc->mod_freq);
	if (cs >= AW_SPI_MAX_CS) {
		device_printf(dev, "Invalid cs %d\n", cs);
		return (EINVAL);
	}

	mtx_lock(&sc->mtx);

	/* Enable and reset the module */
	reg = AW_SPI_READ_4(sc, AW_SPI_GCR);
	reg |= AW_SPI_GCR_EN | AW_SPI_GCR_SRST;
	AW_SPI_WRITE_4(sc, AW_SPI_GCR, reg);

	/* Setup clock, CS and mode */
	aw_spi_setup_clock(sc, clock);
	aw_spi_setup_mode(sc, mode);
	if (cs & SPIBUS_CS_HIGH)
		aw_spi_setup_cs(sc, cs, false);
	else
		aw_spi_setup_cs(sc, cs, true);

	/* xfer */
	err = 0;
	if (cmd->tx_cmd_sz > 0)
		err = aw_spi_xfer(sc, cmd->rx_cmd, cmd->tx_cmd,
		    cmd->tx_cmd_sz, cmd->rx_cmd_sz);
	if (cmd->tx_data_sz > 0 && err == 0)
		err = aw_spi_xfer(sc, cmd->rx_data, cmd->tx_data,
		    cmd->tx_data_sz, cmd->rx_data_sz);

	if (cs & SPIBUS_CS_HIGH)
		aw_spi_setup_cs(sc, cs, true);
	else
		aw_spi_setup_cs(sc, cs, false);

	/* Disable the module */
	reg = AW_SPI_READ_4(sc, AW_SPI_GCR);
	reg &= ~AW_SPI_GCR_EN;
	AW_SPI_WRITE_4(sc, AW_SPI_GCR, reg);

	mtx_unlock(&sc->mtx);

	return (err);
}

static device_method_t aw_spi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_spi_probe),
	DEVMETHOD(device_attach,	aw_spi_attach),
	DEVMETHOD(device_detach,	aw_spi_detach),

        /* spibus_if  */
	DEVMETHOD(spibus_transfer,	aw_spi_transfer),

        /* ofw_bus_if */
	DEVMETHOD(ofw_bus_get_node,	aw_spi_get_node),

	DEVMETHOD_END
};

static driver_t aw_spi_driver = {
	"aw_spi",
	aw_spi_methods,
	sizeof(struct aw_spi_softc),
};

static devclass_t aw_spi_devclass;

DRIVER_MODULE(aw_spi, simplebus, aw_spi_driver, aw_spi_devclass, 0, 0);
DRIVER_MODULE(ofw_spibus, aw_spi, ofw_spibus_driver, ofw_spibus_devclass, 0, 0);
MODULE_DEPEND(aw_spi, ofw_spibus, 1, 1, 1);
SIMPLEBUS_PNP_INFO(compat_data);
