/*-
 * Copyright (c) 2016 Rubicon Communications, LLC (Netgate)
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
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/spibus/spi.h>
#include <dev/spibus/spibusvar.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_hwmods.h>
#include <arm/ti/ti_spireg.h>
#include <arm/ti/ti_spivar.h>

#include "spibus_if.h"

static void ti_spi_intr(void *);
static int ti_spi_detach(device_t);

#undef TI_SPI_DEBUG
#ifdef TI_SPI_DEBUG
#define	IRQSTATUSBITS							\
	"\020\1TX0_EMPTY\2TX0_UNDERFLOW\3RX0_FULL\4RX0_OVERFLOW"	\
	"\5TX1_EMPTY\6TX1_UNDERFLOW\7RX1_FULL\11TX2_EMPTY"		\
	"\12TX1_UNDERFLOW\13RX2_FULL\15TX3_EMPTY\16TX3_UNDERFLOW"	\
	"\17RX3_FULL\22EOW"
#define	CONFBITS							\
	"\020\1PHA\2POL\7EPOL\17DMAW\20DMAR\21DPE0\22DPE1\23IS"		\
	"\24TURBO\25FORCE\30SBE\31SBPOL\34FFEW\35FFER\36CLKG"
#define	STATBITS							\
	"\020\1RXS\2TXS\3EOT\4TXFFE\5TXFFF\6RXFFE\7RXFFFF"
#define	MODULCTRLBITS							\
	"\020\1SINGLE\2NOSPIEN\3SLAVE\4SYST\10MOA\11FDAA"
#define	CTRLBITS							\
	"\020\1ENABLED"

static void
ti_spi_printr(device_t dev)
{
	int clk, conf, ctrl, div, i, j, wl;
	struct ti_spi_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);
	reg = TI_SPI_READ(sc, MCSPI_SYSCONFIG);
	device_printf(dev, "SYSCONFIG: %#x\n", reg);
	reg = TI_SPI_READ(sc, MCSPI_SYSSTATUS);
	device_printf(dev, "SYSSTATUS: %#x\n", reg);
	reg = TI_SPI_READ(sc, MCSPI_IRQSTATUS);
	device_printf(dev, "IRQSTATUS: 0x%b\n", reg, IRQSTATUSBITS);
	reg = TI_SPI_READ(sc, MCSPI_IRQENABLE);
	device_printf(dev, "IRQENABLE: 0x%b\n", reg, IRQSTATUSBITS);
	reg = TI_SPI_READ(sc, MCSPI_MODULCTRL);
	device_printf(dev, "MODULCTRL: 0x%b\n", reg, MODULCTRLBITS);
	for (i = 0; i < sc->sc_numcs; i++) {
		ctrl = TI_SPI_READ(sc, MCSPI_CTRL_CH(i));
		conf = TI_SPI_READ(sc, MCSPI_CONF_CH(i));
		device_printf(dev, "CH%dCONF: 0x%b\n", i, conf, CONFBITS);
		if (conf & MCSPI_CONF_CLKG) {
			div = (conf >> MCSPI_CONF_CLK_SHIFT) & MCSPI_CONF_CLK_MSK;
			div |= ((ctrl >> MCSPI_CTRL_EXTCLK_SHIFT) & MCSPI_CTRL_EXTCLK_MSK) << 4;
		} else {
			div = 1;
			j = (conf >> MCSPI_CONF_CLK_SHIFT) & MCSPI_CONF_CLK_MSK;
			while (j-- > 0)
				div <<= 1;
		}
		clk = TI_SPI_GCLK / div;
		wl = ((conf >> MCSPI_CONF_WL_SHIFT) & MCSPI_CONF_WL_MSK) + 1;
		device_printf(dev, "wordlen: %-2d clock: %d\n", wl, clk);
		reg = TI_SPI_READ(sc, MCSPI_STAT_CH(i));
		device_printf(dev, "CH%dSTAT: 0x%b\n", i, reg, STATBITS);
		device_printf(dev, "CH%dCTRL: 0x%b\n", i, ctrl, CTRLBITS);
	}
	reg = TI_SPI_READ(sc, MCSPI_XFERLEVEL);
	device_printf(dev, "XFERLEVEL: %#x\n", reg);
}
#endif

static void
ti_spi_set_clock(struct ti_spi_softc *sc, int ch, int freq)
{
	uint32_t clkdiv, conf, div, extclk, reg;

	clkdiv = TI_SPI_GCLK / freq;
	if (clkdiv > MCSPI_EXTCLK_MSK) {
		extclk = 0;
		clkdiv = 0;
		div = 1;
		while (TI_SPI_GCLK / div > freq && clkdiv <= 0xf) {
			clkdiv++;
			div <<= 1;
		}
		conf = clkdiv << MCSPI_CONF_CLK_SHIFT;
	} else {
		extclk = clkdiv >> 4;
		clkdiv &= MCSPI_CONF_CLK_MSK;
		conf = MCSPI_CONF_CLKG | clkdiv << MCSPI_CONF_CLK_SHIFT;
	}

	reg = TI_SPI_READ(sc, MCSPI_CTRL_CH(ch));
	reg &= ~(MCSPI_CTRL_EXTCLK_MSK << MCSPI_CTRL_EXTCLK_SHIFT);
	reg |= extclk << MCSPI_CTRL_EXTCLK_SHIFT;
	TI_SPI_WRITE(sc, MCSPI_CTRL_CH(ch), reg);

	reg = TI_SPI_READ(sc, MCSPI_CONF_CH(ch));
	reg &= ~(MCSPI_CONF_CLKG | MCSPI_CONF_CLK_MSK << MCSPI_CONF_CLK_SHIFT);
	TI_SPI_WRITE(sc, MCSPI_CONF_CH(ch), reg | conf);
}

static int
ti_spi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (!ofw_bus_is_compatible(dev, "ti,omap4-mcspi"))
		return (ENXIO);

	device_set_desc(dev, "TI McSPI controller");

	return (BUS_PROBE_DEFAULT);
}

static int
ti_spi_attach(device_t dev)
{
	int clk_id, err, i, rid, timeout;
	struct ti_spi_softc *sc;
	uint32_t rev;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	/*
	 * Get the MMCHS device id from FDT.  If it's not there use the newbus
	 * unit number (which will work as long as the devices are in order and
	 * none are skipped in the fdt).  Note that this is a property we made
	 * up and added in freebsd, it doesn't exist in the published bindings.
	 */
	clk_id = ti_hwmods_get_clock(dev);
	if (clk_id == INVALID_CLK_IDENT) {
		device_printf(dev,
		    "failed to get clock based on hwmods property\n");
		return (EINVAL);
	}

	/* Activate the McSPI module. */
	err = ti_prcm_clk_enable(clk_id);
	if (err) {
		device_printf(dev, "Error: failed to activate source clock\n");
		return (err);
	}

	/* Get the number of available channels. */
	if ((OF_getencprop(ofw_bus_get_node(dev), "ti,spi-num-cs",
	    &sc->sc_numcs, sizeof(sc->sc_numcs))) <= 0) {
		sc->sc_numcs = 2;
	}

	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->sc_mem_res) {
		device_printf(dev, "cannot allocate memory window\n");
		return (ENXIO);
	}

	sc->sc_bst = rman_get_bustag(sc->sc_mem_res);
	sc->sc_bsh = rman_get_bushandle(sc->sc_mem_res);

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
	    NULL, ti_spi_intr, sc, &sc->sc_intrhand)) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		device_printf(dev, "cannot setup the interrupt handler\n");
		return (ENXIO);
	}

	mtx_init(&sc->sc_mtx, "ti_spi", NULL, MTX_DEF);

	/* Issue a softreset to the controller */
	TI_SPI_WRITE(sc, MCSPI_SYSCONFIG, MCSPI_SYSCONFIG_SOFTRESET);
	timeout = 1000;
	while (!(TI_SPI_READ(sc, MCSPI_SYSSTATUS) &
	    MCSPI_SYSSTATUS_RESETDONE)) {
		if (--timeout == 0) {
			device_printf(dev,
			    "Error: Controller reset operation timed out\n");
			ti_spi_detach(dev);
			return (ENXIO);
		}
		DELAY(100);
	}

	/* Print the McSPI module revision. */
	rev = TI_SPI_READ(sc, MCSPI_REVISION);
	device_printf(dev,
	    "scheme: %#x func: %#x rtl: %d rev: %d.%d custom rev: %d\n",
	    (rev >> MCSPI_REVISION_SCHEME_SHIFT) & MCSPI_REVISION_SCHEME_MSK,
	    (rev >> MCSPI_REVISION_FUNC_SHIFT) & MCSPI_REVISION_FUNC_MSK,
	    (rev >> MCSPI_REVISION_RTL_SHIFT) & MCSPI_REVISION_RTL_MSK,
	    (rev >> MCSPI_REVISION_MAJOR_SHIFT) & MCSPI_REVISION_MAJOR_MSK,
	    (rev >> MCSPI_REVISION_MINOR_SHIFT) & MCSPI_REVISION_MINOR_MSK,
	    (rev >> MCSPI_REVISION_CUSTOM_SHIFT) & MCSPI_REVISION_CUSTOM_MSK);

	/* Set Master mode, single channel. */
	TI_SPI_WRITE(sc, MCSPI_MODULCTRL, MCSPI_MODULCTRL_SINGLE);

	/* Clear pending interrupts and disable interrupts. */
	TI_SPI_WRITE(sc, MCSPI_IRQENABLE, 0x0);
	TI_SPI_WRITE(sc, MCSPI_IRQSTATUS, 0xffff);

	for (i = 0; i < sc->sc_numcs; i++) {
		/*
		 * Default to SPI mode 0, CS active low, 8 bits word length and
		 * 500kHz clock.
		 */
		TI_SPI_WRITE(sc, MCSPI_CONF_CH(i),
		    MCSPI_CONF_DPE0 | MCSPI_CONF_EPOL |
		    (8 - 1) << MCSPI_CONF_WL_SHIFT);
		/* Set initial clock - 500kHz. */
		ti_spi_set_clock(sc, i, 500000);
	}

#ifdef	TI_SPI_DEBUG
	ti_spi_printr(dev);
#endif

	device_add_child(dev, "spibus", -1);

	return (bus_generic_attach(dev));
}

static int
ti_spi_detach(device_t dev)
{
	struct ti_spi_softc *sc;

	sc = device_get_softc(dev);

	/* Clear pending interrupts and disable interrupts. */
	TI_SPI_WRITE(sc, MCSPI_IRQENABLE, 0);
	TI_SPI_WRITE(sc, MCSPI_IRQSTATUS, 0xffff);

	/* Reset controller. */
	TI_SPI_WRITE(sc, MCSPI_SYSCONFIG, MCSPI_SYSCONFIG_SOFTRESET);

	bus_generic_detach(dev);

	mtx_destroy(&sc->sc_mtx);
	if (sc->sc_intrhand)
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_intrhand);
	if (sc->sc_irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);

	return (0);
}

static int
ti_spi_fill_fifo(struct ti_spi_softc *sc)
{
	int bytes, timeout;
	struct spi_command *cmd;
	uint32_t written;
	uint8_t *data;

	cmd = sc->sc_cmd;
	bytes = min(sc->sc_len - sc->sc_written, sc->sc_fifolvl);
	while (bytes-- > 0) {
		data = (uint8_t *)cmd->tx_cmd;
		written = sc->sc_written++;
		if (written >= cmd->tx_cmd_sz) {
			data = (uint8_t *)cmd->tx_data;
			written -= cmd->tx_cmd_sz;
		}
		if (sc->sc_fifolvl == 1) {
			/* FIFO disabled. */
			timeout = 1000;
			while (--timeout > 0 && (TI_SPI_READ(sc,
			    MCSPI_STAT_CH(sc->sc_cs)) & MCSPI_STAT_TXS) == 0) {
				DELAY(100);
			}
			if (timeout == 0)
				return (-1);
		}
		TI_SPI_WRITE(sc, MCSPI_TX_CH(sc->sc_cs), data[written]);
	}

	return (0);
}

static int
ti_spi_drain_fifo(struct ti_spi_softc *sc)
{
	int bytes, timeout;
	struct spi_command *cmd;
	uint32_t read;
	uint8_t *data;

	cmd = sc->sc_cmd;
	bytes = min(sc->sc_len - sc->sc_read, sc->sc_fifolvl);
	while (bytes-- > 0) {
		data = (uint8_t *)cmd->rx_cmd;
		read = sc->sc_read++;
		if (read >= cmd->rx_cmd_sz) {
			data = (uint8_t *)cmd->rx_data;
			read -= cmd->rx_cmd_sz;
		}
		if (sc->sc_fifolvl == 1) {
			/* FIFO disabled. */
			timeout = 1000;
			while (--timeout > 0 && (TI_SPI_READ(sc,
			    MCSPI_STAT_CH(sc->sc_cs)) & MCSPI_STAT_RXS) == 0) {
				DELAY(100);
			}
			if (timeout == 0)
				return (-1);
		}
		data[read] = TI_SPI_READ(sc, MCSPI_RX_CH(sc->sc_cs));
	}

	return (0);
}

static void
ti_spi_intr(void *arg)
{
	int eow;
	struct ti_spi_softc *sc;
	uint32_t status;

	eow = 0;
	sc = (struct ti_spi_softc *)arg;
	TI_SPI_LOCK(sc);
	status = TI_SPI_READ(sc, MCSPI_IRQSTATUS);

	/*
	 * No new TX_empty or RX_full event will be asserted while the CPU has
	 * not performed the number of writes or reads defined by
	 * MCSPI_XFERLEVEL[AEL] and MCSPI_XFERLEVEL[AFL].  It is responsibility
	 * of CPU perform the right number of writes and reads.
	 */
	if (status & MCSPI_IRQ_TX0_EMPTY)
		ti_spi_fill_fifo(sc);
	if (status & MCSPI_IRQ_RX0_FULL)
		ti_spi_drain_fifo(sc);

	if (status & MCSPI_IRQ_EOW)
		eow = 1;
		
	/* Clear interrupt status. */
	TI_SPI_WRITE(sc, MCSPI_IRQSTATUS, status);

	/* Check for end of transfer. */
	if (sc->sc_written == sc->sc_len && sc->sc_read == sc->sc_len) {
		sc->sc_flags |= TI_SPI_DONE;
		wakeup(sc->sc_dev);
	}

	TI_SPI_UNLOCK(sc);
}

static int
ti_spi_pio_transfer(struct ti_spi_softc *sc)
{

	while (sc->sc_len - sc->sc_written > 0) {
		if (ti_spi_fill_fifo(sc) == -1)
			return (EIO);
		if (ti_spi_drain_fifo(sc) == -1)
			return (EIO);
	}

	return (0);
}

static int
ti_spi_gcd(int a, int b)
{
	int m;

	while ((m = a % b) != 0) {
		a = b;
		b = m;
	}

	return (b);
}

static int
ti_spi_transfer(device_t dev, device_t child, struct spi_command *cmd)
{
	int err;
	struct ti_spi_softc *sc;
	uint32_t clockhz, cs, mode, reg;

	sc = device_get_softc(dev);

	KASSERT(cmd->tx_cmd_sz == cmd->rx_cmd_sz, 
	    ("TX/RX command sizes should be equal"));
	KASSERT(cmd->tx_data_sz == cmd->rx_data_sz, 
	    ("TX/RX data sizes should be equal"));

	/* Get the proper chip select for this child. */
	spibus_get_cs(child, &cs);
	spibus_get_clock(child, &clockhz);
	spibus_get_mode(child, &mode);

	cs &= ~SPIBUS_CS_HIGH;

	if (cs > sc->sc_numcs) {
		device_printf(dev, "Invalid chip select %d requested by %s\n",
		    cs, device_get_nameunit(child));
		return (EINVAL);
	}

	if (mode > 3)
	{
	    device_printf(dev, "Invalid mode %d requested by %s\n", mode,
		    device_get_nameunit(child));
	    return (EINVAL);
	}

	TI_SPI_LOCK(sc);

	/* If the controller is in use wait until it is available. */
	while (sc->sc_flags & TI_SPI_BUSY)
		mtx_sleep(dev, &sc->sc_mtx, 0, "ti_spi", 0);

	/* Now we have control over SPI controller. */
	sc->sc_flags = TI_SPI_BUSY;

	/* Save the SPI command data. */
	sc->sc_cs = cs;
	sc->sc_cmd = cmd;
	sc->sc_read = 0;
	sc->sc_written = 0;
	sc->sc_len = cmd->tx_cmd_sz + cmd->tx_data_sz;
	sc->sc_fifolvl = ti_spi_gcd(sc->sc_len, TI_SPI_FIFOSZ);
	if (sc->sc_fifolvl < 2 || sc->sc_len > 0xffff)
		sc->sc_fifolvl = 1;	/* FIFO disabled. */
	/* Disable FIFO for now. */
	sc->sc_fifolvl = 1;

	/* Set the bus frequency. */
	ti_spi_set_clock(sc, sc->sc_cs, clockhz);

	/* Disable the FIFO. */
	TI_SPI_WRITE(sc, MCSPI_XFERLEVEL, 0);

	/* 8 bits word, d0 miso, d1 mosi, mode 0 and CS active low. */
	reg = TI_SPI_READ(sc, MCSPI_CONF_CH(sc->sc_cs));
	reg &= ~(MCSPI_CONF_FFER | MCSPI_CONF_FFEW | MCSPI_CONF_SBPOL |
	    MCSPI_CONF_SBE | MCSPI_CONF_TURBO | MCSPI_CONF_IS |
	    MCSPI_CONF_DPE1 | MCSPI_CONF_DPE0 | MCSPI_CONF_DMAR |
	    MCSPI_CONF_DMAW | MCSPI_CONF_EPOL);
	reg |= MCSPI_CONF_DPE0 | MCSPI_CONF_EPOL | MCSPI_CONF_WL8BITS;
	reg |= mode; /* POL and PHA are the low bits, we can just OR-in mode */
	TI_SPI_WRITE(sc, MCSPI_CONF_CH(sc->sc_cs), reg);

#if 0
	/* Enable channel interrupts. */
	reg = TI_SPI_READ(sc, MCSPI_IRQENABLE);
	reg |= 0xf;
	TI_SPI_WRITE(sc, MCSPI_IRQENABLE, reg);
#endif

	/* Start the transfer. */
	reg = TI_SPI_READ(sc, MCSPI_CTRL_CH(sc->sc_cs));
	TI_SPI_WRITE(sc, MCSPI_CTRL_CH(sc->sc_cs), reg | MCSPI_CTRL_ENABLE);

	/* Force CS on. */
	reg = TI_SPI_READ(sc, MCSPI_CONF_CH(sc->sc_cs));
	TI_SPI_WRITE(sc, MCSPI_CONF_CH(sc->sc_cs), reg |= MCSPI_CONF_FORCE);

	err = 0;
	if (sc->sc_fifolvl == 1)
		err = ti_spi_pio_transfer(sc);

	/* Force CS off. */
	reg = TI_SPI_READ(sc, MCSPI_CONF_CH(sc->sc_cs));
	reg &= ~MCSPI_CONF_FORCE;
	TI_SPI_WRITE(sc, MCSPI_CONF_CH(sc->sc_cs), reg);

	/* Disable IRQs. */
	reg = TI_SPI_READ(sc, MCSPI_IRQENABLE);
	reg &= ~0xf;
	TI_SPI_WRITE(sc, MCSPI_IRQENABLE, reg);
	TI_SPI_WRITE(sc, MCSPI_IRQSTATUS, 0xf);

	/* Disable the SPI channel. */
	reg = TI_SPI_READ(sc, MCSPI_CTRL_CH(sc->sc_cs));
	reg &= ~MCSPI_CTRL_ENABLE;
	TI_SPI_WRITE(sc, MCSPI_CTRL_CH(sc->sc_cs), reg);

	/* Disable FIFO. */
	reg = TI_SPI_READ(sc, MCSPI_CONF_CH(sc->sc_cs));
	reg &= ~(MCSPI_CONF_FFER | MCSPI_CONF_FFEW);
	TI_SPI_WRITE(sc, MCSPI_CONF_CH(sc->sc_cs), reg);

	/* Release the controller and wakeup the next thread waiting for it. */
	sc->sc_flags = 0;
	wakeup_one(dev);
	TI_SPI_UNLOCK(sc);

	return (err);
}

static phandle_t
ti_spi_get_node(device_t bus, device_t dev)
{

	/* Share controller node with spibus. */
	return (ofw_bus_get_node(bus));
}

static device_method_t ti_spi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ti_spi_probe),
	DEVMETHOD(device_attach,	ti_spi_attach),
	DEVMETHOD(device_detach,	ti_spi_detach),

	/* SPI interface */
	DEVMETHOD(spibus_transfer,	ti_spi_transfer),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	ti_spi_get_node),

	DEVMETHOD_END
};

static devclass_t ti_spi_devclass;

static driver_t ti_spi_driver = {
	"spi",
	ti_spi_methods,
	sizeof(struct ti_spi_softc),
};

DRIVER_MODULE(ti_spi, simplebus, ti_spi_driver, ti_spi_devclass, 0, 0);
