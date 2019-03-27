/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Ian Lepore <ian@freebsd.org>
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

/*
 * Driver for imx Enhanced Configurable SPI; master-mode only.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/imx/imx_ccmvar.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>
#include <dev/spibus/spi.h>
#include <dev/spibus/spibusvar.h>

#include "spibus_if.h"

#define	ECSPI_RXDATA            0x00
#define	ECSPI_TXDATA            0x04
#define	ECSPI_CTLREG            0x08
#define	  CTLREG_BLEN_SHIFT	  20
#define	  CTLREG_BLEN_MASK	  0x0fff
#define	  CTLREG_CSEL_SHIFT	  18
#define	  CTLREG_CSEL_MASK	  0x03
#define	  CTLREG_DRCTL_SHIFT	  16
#define	  CTLREG_DRCTL_MASK	  0x03
#define	  CTLREG_PREDIV_SHIFT	  12
#define	  CTLREG_PREDIV_MASK	  0x0f
#define	  CTLREG_POSTDIV_SHIFT	  8
#define	  CTLREG_POSTDIV_MASK	  0x0f
#define	  CTLREG_CMODE_SHIFT	  4
#define	  CTLREG_CMODE_MASK	  0x0f
#define	  CTLREG_CMODES_MASTER	  (CTLREG_CMODE_MASK << CTLREG_CMODE_SHIFT)
#define	  CTLREG_SMC		  (1u << 3)
#define	  CTLREG_XCH		  (1u << 2)
#define	  CTLREG_HT		  (1u << 1)
#define	  CTLREG_EN		  (1u << 0)
#define	ECSPI_CFGREG		0x0c
#define	  CFGREG_HTLEN_SHIFT	  24
#define	  CFGREG_SCLKCTL_SHIFT	  20
#define	  CFGREG_DATACTL_SHIFT	  16
#define	  CFGREG_SSPOL_SHIFT	  12
#define	  CFGREG_SSCTL_SHIFT	   8
#define	  CFGREG_SCLKPOL_SHIFT	   4 
#define	  CFGREG_SCLKPHA_SHIFT	   0
#define	  CFGREG_MASK		   0x0f /* all CFGREG fields are 4 bits */
#define	ECSPI_INTREG            0x10
#define	  INTREG_TCEN		  (1u << 7)
#define	  INTREG_ROEN		  (1u << 6)
#define	  INTREG_RFEN		  (1u << 5)
#define	  INTREG_RDREN		  (1u << 4)
#define	  INTREG_RREN		  (1u << 3)
#define	  INTREG_TFEN		  (1u << 2)
#define	  INTREG_TDREN		  (1u << 1)
#define	  INTREG_TEEN		  (1u << 0)
#define	ECSPI_DMAREG            0x14
#define	  DMA_RX_THRESH_SHIFT	  16
#define	  DMA_RX_THRESH_MASK	  0x3f
#define	  DMA_TX_THRESH_SHIFT	  0
#define	  DMA_TX_THRESH_MASK	  0x3f
#define	ECSPI_STATREG           0x18
#define	  SREG_TC		  (1u << 7)
#define	  SREG_RO		  (1u << 6)
#define	  SREG_RF		  (1u << 5)
#define	  SREG_RDR		  (1u << 4)
#define	  SREG_RR		  (1u << 3)
#define	  SREG_TF		  (1u << 2)
#define	  SREG_TDR		  (1u << 1)
#define	  SREG_TE		  (1u << 0)
#define	ECSPI_PERIODREG         0x1c
#define	ECSPI_TESTREG           0x20

#define	CS_MAX		4	/* Max number of chip selects. */
#define	CS_MASK		0x03	/* Mask flag bits out of chipsel. */

#define	FIFO_SIZE	64
#define	FIFO_RXTHRESH	32
#define	FIFO_TXTHRESH	32

struct spi_softc {
	device_t 		dev;
	device_t		spibus;
	struct mtx		mtx;
	struct resource		*memres;
	struct resource		*intres;
	void			*inthandle;
	gpio_pin_t		cspins[CS_MAX];
	u_int			debug;
	u_int			basefreq;
	uint32_t		ctlreg;
	uint32_t		intreg;
	uint32_t		fifocnt;
	uint8_t			*rxbuf;
	uint32_t		rxidx;
	uint32_t		rxlen;
	uint8_t			*txbuf;
	uint32_t		txidx;
	uint32_t		txlen;
};

static struct ofw_compat_data compat_data[] = {
	{"fsl,imx51-ecspi",  true},
	{"fsl,imx53-ecspi",  true},
	{"fsl,imx6dl-ecspi", true},
	{"fsl,imx6q-ecspi",  true},
	{"fsl,imx6sx-ecspi", true},
	{"fsl,imx6ul-ecspi", true},
	{NULL,               false}
};

static inline uint32_t
RD4(struct spi_softc *sc, bus_size_t offset)
{

	return (bus_read_4(sc->memres, offset));
}

static inline void
WR4(struct spi_softc *sc, bus_size_t offset, uint32_t value)
{

	bus_write_4(sc->memres, offset, value);
}

static u_int
spi_calc_clockdiv(struct spi_softc *sc, u_int busfreq)
{
	u_int post, pre;

	/* Returning 0 effectively sets both dividers to 1. */
	if (sc->basefreq <= busfreq)
		return (0);

	/*
	 * Brute-force this; all real-world bus speeds are going to be found on
	 * the 1st or 2nd time through this loop.
	 */
	for (post = 0; post < 16; ++post) {
		pre = ((sc->basefreq >> post) / busfreq) - 1;
		if (pre < 16)
			break;
	}
	if (post == 16) {
		/* The lowest we can go is ~115 Hz. */
		pre = 15;
		post = 15;
	}

	if (sc->debug >= 2) {
		device_printf(sc->dev,
		    "base %u bus %u; pre %u, post %u; actual busfreq %u\n",
		    sc->basefreq, busfreq, pre, post,
		    (sc->basefreq / (pre + 1)) / (1 << post));
	}

	return (pre << CTLREG_PREDIV_SHIFT) | (post << CTLREG_POSTDIV_SHIFT);
}

static void
spi_set_chipsel(struct spi_softc *sc, u_int cs, bool active)
{
	bool pinactive;

	/*
	 * This is kinda crazy... the gpio pins for chipsel are defined as
	 * active-high in the dts, but are supposed to be treated as active-low
	 * by this driver.  So to turn on chipsel we have to invert the value
	 * passed to gpio_pin_set_active().  Then, to make it more fun, any
	 * slave can say its chipsel is active-high, so if that option is
	 * on, we have to invert the value again.
	 */
	pinactive = !active ^ (bool)(cs & SPIBUS_CS_HIGH);

	if (sc->debug >= 2) {
		device_printf(sc->dev, "chipsel %u changed to %u\n",
		    (cs & ~SPIBUS_CS_HIGH), pinactive);
	}

	/*
	 * Change the pin, then do a dummy read of its current state to ensure
	 * that the state change reaches the hardware before proceeding.
	 */
	gpio_pin_set_active(sc->cspins[cs & ~SPIBUS_CS_HIGH], pinactive);
	gpio_pin_is_active(sc->cspins[cs & ~SPIBUS_CS_HIGH], &pinactive);
}

static void
spi_hw_setup(struct spi_softc *sc, u_int cs, u_int mode, u_int freq)
{
	uint32_t reg;

	/*
	 * Set up control register, and write it first to bring the device out
	 * of reset.
	 */
	sc->ctlreg  = CTLREG_EN | CTLREG_CMODES_MASTER | CTLREG_SMC;
	sc->ctlreg |= spi_calc_clockdiv(sc, freq);
	sc->ctlreg |= 7 << CTLREG_BLEN_SHIFT; /* XXX byte at a time */
	WR4(sc, ECSPI_CTLREG, sc->ctlreg);

	/*
	 * Set up the config register.  Note that we do all transfers with the
	 * SPI hardware's chip-select set to zero.  The actual chip select is
	 * handled with a gpio pin.
	 */
	reg = 0;
	if (cs & SPIBUS_CS_HIGH)
		reg |= 1u << CFGREG_SSPOL_SHIFT;
	if (mode & SPIBUS_MODE_CPHA)
		reg |= 1u << CFGREG_SCLKPHA_SHIFT;
	if (mode & SPIBUS_MODE_CPOL) {
		reg |= 1u << CFGREG_SCLKPOL_SHIFT;
		reg |= 1u << CFGREG_SCLKCTL_SHIFT;
	}
	WR4(sc, ECSPI_CFGREG, reg);

	/*
	 * Set up the rx/tx FIFO interrupt thresholds.
	 */
	reg  = (FIFO_RXTHRESH << DMA_RX_THRESH_SHIFT);
	reg |= (FIFO_TXTHRESH << DMA_TX_THRESH_SHIFT);
	WR4(sc, ECSPI_DMAREG, reg);

	/*
	 * Do a dummy read, to make sure the preceding writes reach the spi
	 * hardware before we assert any gpio chip select.
	 */
	(void)RD4(sc, ECSPI_CFGREG);
}

static void
spi_empty_rxfifo(struct spi_softc *sc)
{

	while (sc->rxidx < sc->rxlen && (RD4(sc, ECSPI_STATREG) & SREG_RR)) {
		sc->rxbuf[sc->rxidx++] = (uint8_t)RD4(sc, ECSPI_RXDATA);
		--sc->fifocnt;
	}
}

static void
spi_fill_txfifo(struct spi_softc *sc)
{

	while (sc->txidx < sc->txlen && sc->fifocnt < FIFO_SIZE) {
		WR4(sc, ECSPI_TXDATA, sc->txbuf[sc->txidx++]);
		++sc->fifocnt;
	}

	/*
	 * If we're out of data, disable tx data ready (threshold) interrupts,
	 * and enable tx fifo empty interrupts.
	 */
	if (sc->txidx == sc->txlen)
		sc->intreg = (sc->intreg & ~INTREG_TDREN) | INTREG_TEEN;
}

static void
spi_intr(void *arg)
{
	struct spi_softc *sc = arg;
	uint32_t intreg, status;

	mtx_lock(&sc->mtx);

	sc = arg;
	intreg = sc->intreg;
	status = RD4(sc, ECSPI_STATREG);
	WR4(sc, ECSPI_STATREG, status); /* Clear w1c bits. */

	/*
	 * If we get an overflow error, just signal that the transfer is done
	 * and wakeup the waiting thread, which will see that txidx != txlen and
	 * return an IO error to the caller.
	 */
	if (__predict_false(status & SREG_RO)) {
		if (sc->debug || bootverbose) {
			device_printf(sc->dev, "rxoverflow rxidx %u txidx %u\n",
			    sc->rxidx, sc->txidx);
		}
		sc->intreg = 0;
		wakeup(sc);
		mtx_unlock(&sc->mtx);
		return;
	}

	if (status & SREG_RR)
		spi_empty_rxfifo(sc);

	if (status & SREG_TDR)
		spi_fill_txfifo(sc);

	/*
	 * If we're out of bytes to send...
	 *  - If Transfer Complete is set (shift register is empty) and we've
	 *    received everything we expect, we're all done.
	 *  - Else if Tx Fifo Empty is set, we need to stop waiting for that and
	 *    switch to waiting for Transfer Complete (wait for shift register
	 *    to empty out), and also for Receive Ready (last of incoming data).
	 */
	if (sc->txidx == sc->txlen) {
		if ((status & SREG_TC) && sc->fifocnt == 0) {
			sc->intreg = 0;
			wakeup(sc);
		} else if (status & SREG_TE) {
			sc->intreg &= ~(sc->intreg & ~INTREG_TEEN);
			sc->intreg |= INTREG_TCEN | INTREG_RREN;
		}
	}

	/*
	 * If interrupt flags changed, write the new flags to the hardware and
	 * do a dummy readback to ensure the changes reach the hardware before
	 * we exit the isr.
	 */
	if (sc->intreg != intreg) {
		WR4(sc, ECSPI_INTREG, sc->intreg);
		(void)RD4(sc, ECSPI_INTREG);
	}

	if (sc->debug >= 3) {
		device_printf(sc->dev,
		    "spi_intr, sreg 0x%08x intreg was 0x%08x now 0x%08x\n",
		    status, intreg, sc->intreg);
	}

	mtx_unlock(&sc->mtx);
}

static int
spi_xfer_buf(struct spi_softc *sc, void *rxbuf, void *txbuf, uint32_t len)
{
	int err;

	if (sc->debug >= 1) {
		device_printf(sc->dev,
		    "spi_xfer_buf, rxbuf %p txbuf %p len %u\n",
		    rxbuf, txbuf, len);
	}

	if (len == 0)
		return (0);

	sc->rxbuf = rxbuf;
	sc->rxlen = len;
	sc->rxidx = 0;
	sc->txbuf = txbuf;
	sc->txlen = len;
	sc->txidx = 0;
	sc->intreg = INTREG_RDREN | INTREG_TDREN;
	spi_fill_txfifo(sc);

	/* Enable interrupts last; spi_fill_txfifo() can change sc->intreg */
	WR4(sc, ECSPI_INTREG, sc->intreg);

	err = 0;
	while (err == 0 && sc->intreg != 0)
		err = msleep(sc, &sc->mtx, 0, "imxspi", 10 * hz);

	if (sc->rxidx != sc->rxlen || sc->txidx != sc->txlen)
		err = EIO;

	return (err);
}

static int
spi_transfer(device_t dev, device_t child, struct spi_command *cmd)
{
	struct spi_softc *sc = device_get_softc(dev);
	uint32_t cs, mode, clock;
	int err;

	spibus_get_cs(child, &cs);
	spibus_get_clock(child, &clock);
	spibus_get_mode(child, &mode);

	if (cs > CS_MAX || sc->cspins[cs] == NULL) {
		if (sc->debug || bootverbose)
			device_printf(sc->dev, "Invalid chip select %u\n", cs);
		return (EINVAL);
	}

	mtx_lock(&sc->mtx);
	device_busy(sc->dev);

	if (sc->debug >= 1) {
		device_printf(sc->dev,
		    "spi_transfer, cs 0x%x clock %u mode %u\n",
		    cs, clock, mode);
	}

	/* Set up the hardware and select the device. */
	spi_hw_setup(sc, cs, mode, clock);
	spi_set_chipsel(sc, cs, true);

	/* Transfer command then data bytes. */
	err = 0;
	if (cmd->tx_cmd_sz > 0)
		err = spi_xfer_buf(sc, cmd->rx_cmd, cmd->tx_cmd,
		    cmd->tx_cmd_sz);
	if (cmd->tx_data_sz > 0 && err == 0)
		err = spi_xfer_buf(sc, cmd->rx_data, cmd->tx_data,
		    cmd->tx_data_sz);

	/* Deselect the device, turn off (and reset) hardware. */
	spi_set_chipsel(sc, cs, false);
	WR4(sc, ECSPI_CTLREG, 0);

	device_unbusy(sc->dev);
	mtx_unlock(&sc->mtx);

	return (err);
}

static phandle_t
spi_get_node(device_t bus, device_t dev)
{

	/*
	 * Share our controller node with our spibus child; it instantiates
	 * devices by walking the children contained within our node.
	 */
	return ofw_bus_get_node(bus);
}

static int
spi_detach(device_t dev)
{
	struct spi_softc *sc = device_get_softc(dev);
	int error, idx;

	if ((error = bus_generic_detach(sc->dev)) != 0)
		return (error);

	if (sc->spibus != NULL)
		device_delete_child(dev, sc->spibus);

	for (idx = 0; idx < nitems(sc->cspins); ++idx) {
		if (sc->cspins[idx] != NULL)
			gpio_pin_release(sc->cspins[idx]);
	}

	if (sc->inthandle != NULL)
		bus_teardown_intr(sc->dev, sc->intres, sc->inthandle);
	if (sc->intres != NULL)
		bus_release_resource(sc->dev, SYS_RES_IRQ, 0, sc->intres);
	if (sc->memres != NULL)
		bus_release_resource(sc->dev, SYS_RES_MEMORY, 0, sc->memres);

	mtx_destroy(&sc->mtx);

	return (0);
}

static int
spi_attach(device_t dev)
{
	struct spi_softc *sc = device_get_softc(dev);
	phandle_t node;
	int err, idx, rid;

	sc->dev = dev;
	sc->basefreq = imx_ccm_ecspi_hz();

	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	/* Set up debug-enable sysctl. */
	SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->dev), 
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)),
	    OID_AUTO, "debug", CTLFLAG_RWTUN, &sc->debug, 0,
	    "Enable debug, higher values = more info");

	/* Allocate mmio register access resources. */
	rid = 0;
	sc->memres = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->memres == NULL) {
		device_printf(sc->dev, "could not allocate registers\n");
		spi_detach(sc->dev);
		return (ENXIO);
	}

	/* Allocate interrupt resources and set up handler. */
	rid = 0;
	sc->intres = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->intres == NULL) {
		device_printf(sc->dev, "could not allocate interrupt\n");
		device_detach(sc->dev);
		return (ENXIO);
	}
	err = bus_setup_intr(sc->dev, sc->intres, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, spi_intr, sc, &sc->inthandle);
	if (err != 0) {
		device_printf(sc->dev, "could not setup interrupt handler");
		device_detach(sc->dev);
		return (ENXIO);
	}

	/* Allocate gpio pins for configured chip selects. */
	node = ofw_bus_get_node(sc->dev);
	for (idx = 0; idx < nitems(sc->cspins); ++idx) {
		err = gpio_pin_get_by_ofw_propidx(sc->dev, node, "cs-gpios",
		    idx, &sc->cspins[idx]);
		if (err == 0) {
			gpio_pin_setflags(sc->cspins[idx], GPIO_PIN_OUTPUT);
		} else if (sc->debug >= 2) {
			device_printf(sc->dev,
			    "cannot configure gpio for chip select %u\n", idx);
		}
	}

	/*
	 * Hardware init: put all channels into Master mode, turn off the enable
	 * bit (gates off clocks); we only enable the hardware while xfers run.
	 */
	WR4(sc, ECSPI_CTLREG, CTLREG_CMODES_MASTER);

	/*
	 * Add the spibus driver as a child, and setup a one-shot intrhook to
	 * attach it after interrupts are working.  It will attach actual SPI
	 * devices as its children, and those devices may need to do IO during
	 * their attach. We can't do IO until timers and interrupts are working.
	 */
	sc->spibus = device_add_child(dev, "spibus", -1);
	config_intrhook_oneshot((ich_func_t)bus_generic_attach, dev);

	return (0);
}

static int
spi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "i.MX ECSPI Master");
	return (BUS_PROBE_DEFAULT);
}

static device_method_t spi_methods[] = {
	DEVMETHOD(device_probe,		spi_probe),
	DEVMETHOD(device_attach,	spi_attach),
	DEVMETHOD(device_detach,	spi_detach),

        /* spibus_if  */
	DEVMETHOD(spibus_transfer,	spi_transfer),

        /* ofw_bus_if */
	DEVMETHOD(ofw_bus_get_node,	spi_get_node),

	DEVMETHOD_END
};

static driver_t spi_driver = {
	"imx_spi",
	spi_methods,
	sizeof(struct spi_softc),
};

static devclass_t spi_devclass;

DRIVER_MODULE(imx_spi, simplebus, spi_driver, spi_devclass, 0, 0);
DRIVER_MODULE(ofw_spibus, imx_spi, ofw_spibus_driver, ofw_spibus_devclass, 0, 0);
MODULE_DEPEND(imx_spi, ofw_spibus, 1, 1, 1);
SIMPLEBUS_PNP_INFO(compat_data);
