/*-
 * Copyright (c) 2017-2018, Rubicon Communications, LLC (Netgate)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/spibus/spi.h>
#include <dev/spibus/spibusvar.h>

#include <arm/mv/mvvar.h>

#include "spibus_if.h"

struct mv_spi_softc {
	device_t		sc_dev;
	struct mtx		sc_mtx;
	struct resource		*sc_mem_res;
	struct resource		*sc_irq_res;
	struct spi_command	*sc_cmd;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	uint32_t		sc_len;
	uint32_t		sc_read;
	uint32_t		sc_flags;
	uint32_t		sc_written;
	void			*sc_intrhand;
};

#define	MV_SPI_BUSY		0x1
#define	MV_SPI_WRITE(_sc, _off, _val)		\
    bus_space_write_4((_sc)->sc_bst, (_sc)->sc_bsh, (_off), (_val))
#define	MV_SPI_READ(_sc, _off)			\
    bus_space_read_4((_sc)->sc_bst, (_sc)->sc_bsh, (_off))
#define	MV_SPI_LOCK(_sc)	mtx_lock(&(_sc)->sc_mtx)
#define	MV_SPI_UNLOCK(_sc)	mtx_unlock(&(_sc)->sc_mtx)

#define	MV_SPI_CONTROL		0
#define	MV_SPI_CTRL_CS_MASK		7
#define	MV_SPI_CTRL_CS_SHIFT		2
#define	MV_SPI_CTRL_SMEMREADY		(1 << 1)
#define	MV_SPI_CTRL_CS_ACTIVE		(1 << 0)
#define	MV_SPI_CONF		0x4
#define	MV_SPI_CONF_MODE_SHIFT		12
#define	MV_SPI_CONF_MODE_MASK		(3 << MV_SPI_CONF_MODE_SHIFT)
#define	MV_SPI_CONF_BYTELEN		(1 << 5)
#define	MV_SPI_CONF_CLOCK_SPR_MASK	0xf
#define	MV_SPI_CONF_CLOCK_SPPR_MASK	1
#define	MV_SPI_CONF_CLOCK_SPPR_SHIFT	4
#define	MV_SPI_CONF_CLOCK_SPPRHI_MASK	3
#define	MV_SPI_CONF_CLOCK_SPPRHI_SHIFT	6
#define	MV_SPI_CONF_CLOCK_MASK						\
    ((MV_SPI_CONF_CLOCK_SPPRHI_MASK << MV_SPI_CONF_CLOCK_SPPRHI_SHIFT) | \
    (MV_SPI_CONF_CLOCK_SPPR_MASK << MV_SPI_CONF_CLOCK_SPPR_SHIFT) |	\
    MV_SPI_CONF_CLOCK_SPR_MASK)
#define	MV_SPI_DATAOUT		0x8
#define	MV_SPI_DATAIN		0xc
#define	MV_SPI_INTR_STAT	0x10
#define	MV_SPI_INTR_MASK	0x14
#define	MV_SPI_INTR_SMEMREADY		(1 << 0)

static struct ofw_compat_data compat_data[] = {
        {"marvell,armada-380-spi",	1},
        {NULL,                          0}
};

static void mv_spi_intr(void *);

static int
mv_spi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Marvell SPI controller");

	return (BUS_PROBE_DEFAULT);
}

static int
mv_spi_attach(device_t dev)
{
	struct mv_spi_softc *sc;
	int rid;
	uint32_t reg;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

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

	/* Deactivate the bus - just in case... */
	reg = MV_SPI_READ(sc, MV_SPI_CONTROL);
	MV_SPI_WRITE(sc, MV_SPI_CONTROL, reg & ~MV_SPI_CTRL_CS_ACTIVE);

	/* Disable the two bytes FIFO. */
	reg = MV_SPI_READ(sc, MV_SPI_CONF);
	MV_SPI_WRITE(sc, MV_SPI_CONF, reg & ~MV_SPI_CONF_BYTELEN);

	/* Clear and disable interrupts. */
	MV_SPI_WRITE(sc, MV_SPI_INTR_MASK, 0);
	MV_SPI_WRITE(sc, MV_SPI_INTR_STAT, 0);

	/* Hook up our interrupt handler. */
	if (bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, mv_spi_intr, sc, &sc->sc_intrhand)) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		device_printf(dev, "cannot setup the interrupt handler\n");
		return (ENXIO);
	}

	mtx_init(&sc->sc_mtx, "mv_spi", NULL, MTX_DEF);

	device_add_child(dev, "spibus", -1);

	/* Probe and attach the spibus when interrupts are available. */
	config_intrhook_oneshot((ich_func_t)bus_generic_attach, dev);

	return (0);
}

static int
mv_spi_detach(device_t dev)
{
	struct mv_spi_softc *sc;

	bus_generic_detach(dev);

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

static __inline void
mv_spi_rx_byte(struct mv_spi_softc *sc)
{
	struct spi_command *cmd;
	uint32_t read;
	uint8_t *p;

	cmd = sc->sc_cmd; 
	p = (uint8_t *)cmd->rx_cmd;
	read = sc->sc_read++;
	if (read >= cmd->rx_cmd_sz) {
		p = (uint8_t *)cmd->rx_data;
		read -= cmd->rx_cmd_sz;
	}
	p[read] = MV_SPI_READ(sc, MV_SPI_DATAIN) & 0xff;
}

static __inline void
mv_spi_tx_byte(struct mv_spi_softc *sc)
{
	struct spi_command *cmd;
	uint32_t written;
	uint8_t *p;

	cmd = sc->sc_cmd; 
	p = (uint8_t *)cmd->tx_cmd;
	written = sc->sc_written++;
	if (written >= cmd->tx_cmd_sz) {
		p = (uint8_t *)cmd->tx_data;
		written -= cmd->tx_cmd_sz;
	}
	MV_SPI_WRITE(sc, MV_SPI_DATAOUT, p[written]);
}

static void
mv_spi_intr(void *arg)
{
	struct mv_spi_softc *sc;

	sc = (struct mv_spi_softc *)arg;
	MV_SPI_LOCK(sc);

	/* Filter stray interrupts. */
	if ((sc->sc_flags & MV_SPI_BUSY) == 0) {
		MV_SPI_UNLOCK(sc);
		return;
	}

	/* RX */
	mv_spi_rx_byte(sc);

	/* TX */
	mv_spi_tx_byte(sc);

	/* Check for end of transfer. */
	if (sc->sc_written == sc->sc_len && sc->sc_read == sc->sc_len)
		wakeup(sc->sc_dev);

	MV_SPI_UNLOCK(sc);
}

static int
mv_spi_psc_calc(uint32_t clock, uint32_t *spr, uint32_t *sppr)
{
	uint32_t divider, tclk;

	tclk = get_tclk_armada38x();
	for (*spr = 2; *spr <= 15; (*spr)++) {
		for (*sppr = 0; *sppr <= 7; (*sppr)++) {
			divider = *spr * (1 << *sppr);
			if (tclk / divider <= clock)
				return (0);
		}
	}

	return (EINVAL);
}

static int
mv_spi_transfer(device_t dev, device_t child, struct spi_command *cmd)
{
	struct mv_spi_softc *sc;
	uint32_t clock, cs, mode, reg, spr, sppr;
	int resid, timeout;

	KASSERT(cmd->tx_cmd_sz == cmd->rx_cmd_sz,
	    ("TX/RX command sizes should be equal"));
	KASSERT(cmd->tx_data_sz == cmd->rx_data_sz,
	    ("TX/RX data sizes should be equal"));

	/* Get the proper chip select, mode and clock for this transfer. */
	spibus_get_cs(child, &cs);
	cs &= ~SPIBUS_CS_HIGH;
	spibus_get_mode(child, &mode);
	if (mode > 3) {
		device_printf(dev,
		    "Invalid mode %u requested by %s\n", mode,
		    device_get_nameunit(child));
		return (EINVAL);
	}
	spibus_get_clock(child, &clock);
	if (clock == 0 || mv_spi_psc_calc(clock, &spr, &sppr) != 0) {
		device_printf(dev,
		    "Invalid clock %uHz requested by %s\n", clock,
		    device_get_nameunit(child));
		return (EINVAL);
	}

	sc = device_get_softc(dev);
	MV_SPI_LOCK(sc);

	/* Wait until the controller is free. */
	while (sc->sc_flags & MV_SPI_BUSY)
		mtx_sleep(dev, &sc->sc_mtx, 0, "mv_spi", 0);

	/* Now we have control over SPI controller. */
	sc->sc_flags = MV_SPI_BUSY;

	/* Save a pointer to the SPI command. */
	sc->sc_cmd = cmd;
	sc->sc_read = 0;
	sc->sc_written = 0;
	sc->sc_len = cmd->tx_cmd_sz + cmd->tx_data_sz;

	/* Set SPI Mode and Clock. */
	reg = MV_SPI_READ(sc, MV_SPI_CONF);
	reg &= ~(MV_SPI_CONF_MODE_MASK | MV_SPI_CONF_CLOCK_MASK);
	reg |= mode << MV_SPI_CONF_MODE_SHIFT;
	reg |= spr & MV_SPI_CONF_CLOCK_SPR_MASK;
	reg |= (sppr & MV_SPI_CONF_CLOCK_SPPR_MASK) <<
	    MV_SPI_CONF_CLOCK_SPPR_SHIFT;
	reg |= (sppr & MV_SPI_CONF_CLOCK_SPPRHI_MASK) <<
	    MV_SPI_CONF_CLOCK_SPPRHI_SHIFT;
	MV_SPI_WRITE(sc, MV_SPI_CONTROL, reg);

	/* Set CS number and assert CS. */
	reg = (cs & MV_SPI_CTRL_CS_MASK) << MV_SPI_CTRL_CS_SHIFT;
	MV_SPI_WRITE(sc, MV_SPI_CONTROL, reg);
	reg = MV_SPI_READ(sc, MV_SPI_CONTROL);
	MV_SPI_WRITE(sc, MV_SPI_CONTROL, reg | MV_SPI_CTRL_CS_ACTIVE);

	while ((resid = sc->sc_len - sc->sc_written) > 0) {

		MV_SPI_WRITE(sc, MV_SPI_INTR_STAT, 0);

		/*
		 * Write to start the transmission and read the byte
		 * back when ready.
		 */
		mv_spi_tx_byte(sc);
		timeout = 1000;
		while (--timeout > 0) {
			reg = MV_SPI_READ(sc, MV_SPI_CONTROL);
			if (reg & MV_SPI_CTRL_SMEMREADY)
				break;
			DELAY(1);
		}
		if (timeout == 0)
			break;
		mv_spi_rx_byte(sc);
	}

	/* Stop the controller. */
	reg = MV_SPI_READ(sc, MV_SPI_CONTROL);
	MV_SPI_WRITE(sc, MV_SPI_CONTROL, reg & ~MV_SPI_CTRL_CS_ACTIVE);
	MV_SPI_WRITE(sc, MV_SPI_INTR_MASK, 0);
	MV_SPI_WRITE(sc, MV_SPI_INTR_STAT, 0);

	/* Release the controller and wakeup the next thread waiting for it. */
	sc->sc_flags = 0;
	wakeup_one(dev);
	MV_SPI_UNLOCK(sc);

	/*
	 * Check for transfer timeout.  The SPI controller doesn't
	 * return errors.
	 */
	return ((timeout == 0) ? EIO : 0);
}

static phandle_t
mv_spi_get_node(device_t bus, device_t dev)
{

	return (ofw_bus_get_node(bus));
}

static device_method_t mv_spi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mv_spi_probe),
	DEVMETHOD(device_attach,	mv_spi_attach),
	DEVMETHOD(device_detach,	mv_spi_detach),

	/* SPI interface */
	DEVMETHOD(spibus_transfer,	mv_spi_transfer),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	mv_spi_get_node),

	DEVMETHOD_END
};

static devclass_t mv_spi_devclass;

static driver_t mv_spi_driver = {
	"spi",
	mv_spi_methods,
	sizeof(struct mv_spi_softc),
};

DRIVER_MODULE(mv_spi, simplebus, mv_spi_driver, mv_spi_devclass, 0, 0);
