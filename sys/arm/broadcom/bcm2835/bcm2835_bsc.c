/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Tsubai Masanari.
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * Copyright (c) 2013 Luiz Otavio O Souza <loos@freebsd.org>
 * Copyright (c) 2017 Ian Lepore <ian@freebsd.org>
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

/*
 * Driver for bcm2835 i2c-compatible two-wire bus, named 'BSC' on this SoC.
 *
 * This controller can only perform complete transfers, it does not provide
 * low-level control over sending start/repeat-start/stop sequences on the bus.
 * In addition, bugs in the silicon make it somewhat difficult to perform a
 * repeat-start, and limit the repeat-start to a read following a write on
 * the same slave device.  (The i2c protocol allows a repeat start to change
 * direction or not, and change slave address or not at any time.)
 *
 * The repeat-start bug and workaround are described in a problem report at
 * https://github.com/raspberrypi/linux/issues/254 with the crucial part being
 * in a comment block from a fragment of a GPU i2c driver, containing this:
 *
 * -----------------------------------------------------------------------------
 * - See i2c.v: The I2C peripheral samples the values for rw_bit and xfer_count
 * - in the IDLE state if start is set.
 * - 
 * - We want to generate a ReSTART not a STOP at the end of the TX phase. In
 * - order to do that we must ensure the state machine goes RACK1 -> RACK2 ->
 * - SRSTRT1 (not RACK1 -> RACK2 -> SSTOP1).
 * - 
 * - So, in the RACK2 state when (TX) xfer_count==0 we must therefore have
 * - already set, ready to be sampled:
 * -  READ ; rw_bit     <= I2CC bit 0 -- must be "read"
 * -  ST;    start      <= I2CC bit 7 -- must be "Go" in order to not issue STOP
 * -  DLEN;  xfer_count <= I2CDLEN    -- must be equal to our read amount
 * - 
 * - The plan to do this is:
 * -  1. Start the sub-address write, but don't let it finish
 * -     (keep xfer_count > 0)
 * -  2. Populate READ, DLEN and ST in preparation for ReSTART read sequence
 * -  3. Let TX finish (write the rest of the data)
 * -  4. Read back data as it arrives
 * -----------------------------------------------------------------------------
 *
 * The transfer function below scans the list of messages passed to it, looking
 * for a read following a write to the same slave.  When it finds that, it
 * starts the write without prefilling the tx fifo, which holds xfer_count>0,
 * then presets the direction, length, and start command for the following read,
 * as described above.  Then the tx fifo is filled and the rest of the transfer
 * proceeds as normal, with the controller automatically supplying a
 * repeat-start on the bus when the write operation finishes.
 *
 * XXX I suspect the controller may be able to do a repeat-start on any
 * write->read or write->write transition, even when the slave addresses differ.
 * It's unclear whether the slave address can be prestaged along with the
 * direction and length while the write xfer_count is being held at zero.  In
 * fact, if it can't do this, then it couldn't be used to read EDID data.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/broadcom/bcm2835/bcm2835_bscreg.h>
#include <arm/broadcom/bcm2835/bcm2835_bscvar.h>

#include "iicbus_if.h"

static struct ofw_compat_data compat_data[] = {
	{"broadcom,bcm2835-bsc",	1},
	{"brcm,bcm2708-i2c",		1},
	{"brcm,bcm2835-i2c",		1},
	{NULL,				0}
};

#define DEVICE_DEBUGF(sc, lvl, fmt, args...) \
    if ((lvl) <= (sc)->sc_debug) \
        device_printf((sc)->sc_dev, fmt, ##args)

#define DEBUGF(sc, lvl, fmt, args...) \
    if ((lvl) <= (sc)->sc_debug) \
        printf(fmt, ##args)

static void bcm_bsc_intr(void *);
static int bcm_bsc_detach(device_t);

static void
bcm_bsc_modifyreg(struct bcm_bsc_softc *sc, uint32_t off, uint32_t mask,
	uint32_t value)
{
	uint32_t reg;

	mtx_assert(&sc->sc_mtx, MA_OWNED);        
	reg = BCM_BSC_READ(sc, off);
	reg &= ~mask;
	reg |= value;
	BCM_BSC_WRITE(sc, off, reg);
}

static int
bcm_bsc_clock_proc(SYSCTL_HANDLER_ARGS)
{
	struct bcm_bsc_softc *sc;
	uint32_t clk;

	sc = (struct bcm_bsc_softc *)arg1;
	BCM_BSC_LOCK(sc);
	clk = BCM_BSC_READ(sc, BCM_BSC_CLOCK);
	BCM_BSC_UNLOCK(sc);
	clk &= 0xffff;
	if (clk == 0)
		clk = 32768;
	clk = BCM_BSC_CORE_CLK / clk;

	return (sysctl_handle_int(oidp, &clk, 0, req));
}

static int
bcm_bsc_clkt_proc(SYSCTL_HANDLER_ARGS)
{
	struct bcm_bsc_softc *sc;
	uint32_t clkt;
	int error;

	sc = (struct bcm_bsc_softc *)arg1;

	BCM_BSC_LOCK(sc);
	clkt = BCM_BSC_READ(sc, BCM_BSC_CLKT);
	BCM_BSC_UNLOCK(sc);
	clkt &= 0xffff;
	error = sysctl_handle_int(oidp, &clkt, sizeof(clkt), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	BCM_BSC_LOCK(sc);
	BCM_BSC_WRITE(sc, BCM_BSC_CLKT, clkt & 0xffff);
	BCM_BSC_UNLOCK(sc);

	return (0);
}

static int
bcm_bsc_fall_proc(SYSCTL_HANDLER_ARGS)
{
	struct bcm_bsc_softc *sc;
	uint32_t clk, reg;
	int error;

	sc = (struct bcm_bsc_softc *)arg1;

	BCM_BSC_LOCK(sc);
	reg = BCM_BSC_READ(sc, BCM_BSC_DELAY);
	BCM_BSC_UNLOCK(sc);
	reg >>= 16;
	error = sysctl_handle_int(oidp, &reg, sizeof(reg), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	BCM_BSC_LOCK(sc);
	clk = BCM_BSC_READ(sc, BCM_BSC_CLOCK);
	clk = BCM_BSC_CORE_CLK / clk;
	if (reg > clk / 2)
		reg = clk / 2 - 1;
	bcm_bsc_modifyreg(sc, BCM_BSC_DELAY, 0xffff0000, reg << 16);
	BCM_BSC_UNLOCK(sc);

	return (0);
}

static int
bcm_bsc_rise_proc(SYSCTL_HANDLER_ARGS)
{
	struct bcm_bsc_softc *sc;
	uint32_t clk, reg;
	int error;

	sc = (struct bcm_bsc_softc *)arg1;

	BCM_BSC_LOCK(sc);
	reg = BCM_BSC_READ(sc, BCM_BSC_DELAY);
	BCM_BSC_UNLOCK(sc);
	reg &= 0xffff;
	error = sysctl_handle_int(oidp, &reg, sizeof(reg), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	BCM_BSC_LOCK(sc);
	clk = BCM_BSC_READ(sc, BCM_BSC_CLOCK);
	clk = BCM_BSC_CORE_CLK / clk;
	if (reg > clk / 2)
		reg = clk / 2 - 1;
	bcm_bsc_modifyreg(sc, BCM_BSC_DELAY, 0xffff, reg);
	BCM_BSC_UNLOCK(sc);

	return (0);
}

static void
bcm_bsc_sysctl_init(struct bcm_bsc_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree_node;
	struct sysctl_oid_list *tree;

	/*
	 * Add system sysctl tree/handlers.
	 */
	ctx = device_get_sysctl_ctx(sc->sc_dev);
	tree_node = device_get_sysctl_tree(sc->sc_dev);
	tree = SYSCTL_CHILDREN(tree_node);
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "frequency",
	    CTLFLAG_RW | CTLTYPE_UINT, sc, sizeof(*sc),
	    bcm_bsc_clock_proc, "IU", "I2C BUS clock frequency");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "clock_stretch",
	    CTLFLAG_RW | CTLTYPE_UINT, sc, sizeof(*sc),
	    bcm_bsc_clkt_proc, "IU", "I2C BUS clock stretch timeout");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "fall_edge_delay",
	    CTLFLAG_RW | CTLTYPE_UINT, sc, sizeof(*sc),
	    bcm_bsc_fall_proc, "IU", "I2C BUS falling edge delay");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "rise_edge_delay",
	    CTLFLAG_RW | CTLTYPE_UINT, sc, sizeof(*sc),
	    bcm_bsc_rise_proc, "IU", "I2C BUS rising edge delay");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "debug",
	    CTLFLAG_RWTUN, &sc->sc_debug, 0,
	    "Enable debug; 1=reads/writes, 2=add starts/stops");
}

static void
bcm_bsc_reset(struct bcm_bsc_softc *sc)
{

	/* Enable the BSC Controller, disable interrupts. */
	BCM_BSC_WRITE(sc, BCM_BSC_CTRL, BCM_BSC_CTRL_I2CEN);
	/* Clear pending interrupts. */
	BCM_BSC_WRITE(sc, BCM_BSC_STATUS, BCM_BSC_STATUS_CLKT |
	    BCM_BSC_STATUS_ERR | BCM_BSC_STATUS_DONE);
	/* Clear the FIFO. */
	bcm_bsc_modifyreg(sc, BCM_BSC_CTRL, BCM_BSC_CTRL_CLEAR0,
	    BCM_BSC_CTRL_CLEAR0);
}

static int
bcm_bsc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "BCM2708/2835 BSC controller");

	return (BUS_PROBE_DEFAULT);
}

static int
bcm_bsc_attach(device_t dev)
{
	struct bcm_bsc_softc *sc;
	int rid;

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
	    RF_ACTIVE | RF_SHAREABLE);
	if (!sc->sc_irq_res) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		device_printf(dev, "cannot allocate interrupt\n");
		return (ENXIO);
	}

	/* Hook up our interrupt handler. */
	if (bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, bcm_bsc_intr, sc, &sc->sc_intrhand)) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		device_printf(dev, "cannot setup the interrupt handler\n");
		return (ENXIO);
	}

	mtx_init(&sc->sc_mtx, "bcm_bsc", NULL, MTX_DEF);

	bcm_bsc_sysctl_init(sc);

	/* Enable the BSC controller.  Flush the FIFO. */
	BCM_BSC_LOCK(sc);
	bcm_bsc_reset(sc);
	BCM_BSC_UNLOCK(sc);

	sc->sc_iicbus = device_add_child(dev, "iicbus", -1);
	if (sc->sc_iicbus == NULL) {
		bcm_bsc_detach(dev);
		return (ENXIO);
	}

	/* Probe and attach the iicbus when interrupts are available. */
	config_intrhook_oneshot((ich_func_t)bus_generic_attach, dev);

	return (0);
}

static int
bcm_bsc_detach(device_t dev)
{
	struct bcm_bsc_softc *sc;

	bus_generic_detach(dev);

	sc = device_get_softc(dev);
	if (sc->sc_iicbus != NULL)
		device_delete_child(dev, sc->sc_iicbus);
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
bcm_bsc_empty_rx_fifo(struct bcm_bsc_softc *sc)
{
	uint32_t status;

	/* Assumes sc_totlen > 0 and BCM_BSC_STATUS_RXD is asserted on entry. */
	do {
		if (sc->sc_resid == 0) {
			sc->sc_data  = sc->sc_curmsg->buf;
			sc->sc_dlen  = sc->sc_curmsg->len;
			sc->sc_resid = sc->sc_dlen;
			++sc->sc_curmsg;
		}
		do {
			*sc->sc_data = BCM_BSC_READ(sc, BCM_BSC_DATA);
			DEBUGF(sc, 1, "0x%02x ", *sc->sc_data); 
			++sc->sc_data;
			--sc->sc_resid;
			--sc->sc_totlen;
			status = BCM_BSC_READ(sc, BCM_BSC_STATUS);
		} while (sc->sc_resid > 0 && (status & BCM_BSC_STATUS_RXD));
	} while (sc->sc_totlen > 0 && (status & BCM_BSC_STATUS_RXD));
}

static void
bcm_bsc_fill_tx_fifo(struct bcm_bsc_softc *sc)
{
	uint32_t status;

	/* Assumes sc_totlen > 0 and BCM_BSC_STATUS_TXD is asserted on entry. */
	do {
		if (sc->sc_resid == 0) {
			sc->sc_data  = sc->sc_curmsg->buf;
			sc->sc_dlen  = sc->sc_curmsg->len;
			sc->sc_resid = sc->sc_dlen;
			++sc->sc_curmsg;
		}
		do {
			BCM_BSC_WRITE(sc, BCM_BSC_DATA, *sc->sc_data);
			DEBUGF(sc, 1, "0x%02x ", *sc->sc_data); 
			++sc->sc_data;
			--sc->sc_resid;
			--sc->sc_totlen;
			status = BCM_BSC_READ(sc, BCM_BSC_STATUS);
		} while (sc->sc_resid > 0 && (status & BCM_BSC_STATUS_TXD));
		/*
		 * If a repeat-start was pending and we just hit the end of a tx
		 * buffer, see if it's also the end of the writes that preceeded
		 * the repeat-start.  If so, log the repeat-start and the start
		 * of the following read, and return because we're not writing
		 * anymore (and TXD will be true because there's room to write
		 * in the fifo).
		 */
		if (sc->sc_replen > 0 && sc->sc_resid == 0) {
			sc->sc_replen -= sc->sc_dlen;
			if (sc->sc_replen == 0) {
				DEBUGF(sc, 1, " err=0\n");
				DEVICE_DEBUGF(sc, 2, "rstart 0x%02x\n",
				    sc->sc_curmsg->slave | 0x01);
				DEVICE_DEBUGF(sc, 1,
				    "read   0x%02x len %d: ",
				    sc->sc_curmsg->slave | 0x01,
				    sc->sc_totlen);
				sc->sc_flags |= BCM_I2C_READ;
				return;
			}
		}
	} while (sc->sc_totlen > 0 && (status & BCM_BSC_STATUS_TXD));
}

static void
bcm_bsc_intr(void *arg)
{
	struct bcm_bsc_softc *sc;
	uint32_t status;

	sc = (struct bcm_bsc_softc *)arg;

	BCM_BSC_LOCK(sc);

	/* The I2C interrupt is shared among all the BSC controllers. */
	if ((sc->sc_flags & BCM_I2C_BUSY) == 0) {
		BCM_BSC_UNLOCK(sc);
		return;
	}

	status = BCM_BSC_READ(sc, BCM_BSC_STATUS);
	DEBUGF(sc, 4, " <intrstatus=0x%08x> ", status);

	/* RXD and DONE can assert together, empty fifo before checking done. */
	if ((sc->sc_flags & BCM_I2C_READ) && (status & BCM_BSC_STATUS_RXD))
		bcm_bsc_empty_rx_fifo(sc);

	/* Check for completion. */
	if (status & (BCM_BSC_STATUS_ERRBITS | BCM_BSC_STATUS_DONE)) {
		sc->sc_flags |= BCM_I2C_DONE;
		if (status & BCM_BSC_STATUS_ERRBITS)
			sc->sc_flags |= BCM_I2C_ERROR;
		/* Disable interrupts. */
		bcm_bsc_reset(sc);
		wakeup(sc);
	} else if (!(sc->sc_flags & BCM_I2C_READ)) {
		/*
		 * Don't check for TXD until after determining whether the
		 * transfer is complete; TXD will be asserted along with ERR or
		 * DONE if there is room in the fifo.
		 */
		if ((status & BCM_BSC_STATUS_TXD) && sc->sc_totlen > 0)
			bcm_bsc_fill_tx_fifo(sc);
	}

	BCM_BSC_UNLOCK(sc);
}

static int
bcm_bsc_transfer(device_t dev, struct iic_msg *msgs, uint32_t nmsgs)
{
	struct bcm_bsc_softc *sc;
	struct iic_msg *endmsgs, *nxtmsg;
	uint32_t readctl, status;
	int err;
	uint16_t curlen;
	uint8_t curisread, curslave, nxtisread, nxtslave;

	sc = device_get_softc(dev);
	BCM_BSC_LOCK(sc);

	/* If the controller is busy wait until it is available. */
	while (sc->sc_flags & BCM_I2C_BUSY)
		mtx_sleep(dev, &sc->sc_mtx, 0, "bscbusw", 0);

	/* Now we have control over the BSC controller. */
	sc->sc_flags = BCM_I2C_BUSY;

	DEVICE_DEBUGF(sc, 3, "Transfer %d msgs\n", nmsgs);

	/* Clear the FIFO and the pending interrupts. */
	bcm_bsc_reset(sc);

	/*
	 * Perform all the transfers requested in the array of msgs.  Note that
	 * it is bcm_bsc_empty_rx_fifo() and bcm_bsc_fill_tx_fifo() that advance
	 * sc->sc_curmsg through the array of messages, as the data from each
	 * message is fully consumed, but it is this loop that notices when we
	 * have no more messages to process.
	 */
	err = 0;
	sc->sc_resid = 0;
	sc->sc_curmsg = msgs;
	endmsgs = &msgs[nmsgs];
	while (sc->sc_curmsg < endmsgs) {
		readctl = 0;
		curslave = sc->sc_curmsg->slave >> 1;
		curisread = sc->sc_curmsg->flags & IIC_M_RD;
		sc->sc_replen = 0;
		sc->sc_totlen = sc->sc_curmsg->len;
		/*
		 * Scan for scatter/gather IO (same slave and direction) or
		 * repeat-start (read following write for the same slave).
		 */
		for (nxtmsg = sc->sc_curmsg + 1; nxtmsg < endmsgs; ++nxtmsg) {
			nxtslave = nxtmsg->slave >> 1;
			if (curslave == nxtslave) {
				nxtisread = nxtmsg->flags & IIC_M_RD;
				if (curisread == nxtisread) {
					/*
					 * Same slave and direction, this
					 * message will be part of the same
					 * transfer as the previous one.
					 */
					sc->sc_totlen += nxtmsg->len;
					continue;
				} else if (curisread == IIC_M_WR) {
					/*
					 * Read after write to same slave means
					 * repeat-start, remember how many bytes
					 * come before the repeat-start, switch
					 * the direction to IIC_M_RD, and gather
					 * up following reads to the same slave.
					 */
					curisread = IIC_M_RD;
					sc->sc_replen = sc->sc_totlen;
					sc->sc_totlen += nxtmsg->len;
					continue;
				}
			}
			break;
		}

		/*
		 * curslave and curisread temporaries from above may refer to
		 * the after-repstart msg, reset them to reflect sc_curmsg.
		 */
		curisread = (sc->sc_curmsg->flags & IIC_M_RD) ? 1 : 0;
		curslave = sc->sc_curmsg->slave | curisread;

		/* Write the slave address. */
		BCM_BSC_WRITE(sc, BCM_BSC_SLAVE, curslave >> 1);

		DEVICE_DEBUGF(sc, 2, "start  0x%02x\n", curslave);

		/*
		 * Either set up read length and direction variables for a
		 * simple transfer or get the hardware started on the first
		 * piece of a transfer that involves a repeat-start and set up
		 * the read length and direction vars for the second piece.
		 */
		if (sc->sc_replen == 0) {
			DEVICE_DEBUGF(sc, 1, "%-6s 0x%02x len %d: ", 
			    (curisread) ? "read" : "write", curslave,
			    sc->sc_totlen);
			curlen = sc->sc_totlen;
			if (curisread) {
				readctl = BCM_BSC_CTRL_READ;
				sc->sc_flags |= BCM_I2C_READ;
			} else {
				readctl = 0;
				sc->sc_flags &= ~BCM_I2C_READ;
			}
		} else {
			DEVICE_DEBUGF(sc, 1, "%-6s 0x%02x len %d: ", 
			    (curisread) ? "read" : "write", curslave,
			    sc->sc_replen);

			/*
			 * Start the write transfer with an empty fifo and wait
			 * for the 'transfer active' status bit to light up;
			 * that indicates that the hardware has latched the
			 * direction and length for the write, and we can safely
			 * reload those registers and issue the start for the
			 * following read; interrupts are not enabled here.
			 */
			BCM_BSC_WRITE(sc, BCM_BSC_DLEN, sc->sc_replen);
			BCM_BSC_WRITE(sc, BCM_BSC_CTRL, BCM_BSC_CTRL_I2CEN |
			    BCM_BSC_CTRL_ST);
			do {
				status = BCM_BSC_READ(sc, BCM_BSC_STATUS);
				if (status & BCM_BSC_STATUS_ERR) {
					/* no ACK on slave addr */
					err = EIO;
					goto xfer_done;
				}
			} while ((status & BCM_BSC_STATUS_TA) == 0);
			/*
			 * Set curlen and readctl for the repeat-start read that
			 * we need to set up below, but set sc_flags to write,
			 * because that is the operation in progress right now.
			 */
			curlen = sc->sc_totlen - sc->sc_replen;
			readctl = BCM_BSC_CTRL_READ;
			sc->sc_flags &= ~BCM_I2C_READ;
		}

		/*
		 * Start the transfer with interrupts enabled, then if doing a
		 * write, fill the tx fifo.  Not prefilling the fifo until after
		 * this start command is the key workaround for making
		 * repeat-start work, and it's harmless to do it in this order
		 * for a regular write too.
		 */
		BCM_BSC_WRITE(sc, BCM_BSC_DLEN, curlen);
		BCM_BSC_WRITE(sc, BCM_BSC_CTRL, readctl | BCM_BSC_CTRL_I2CEN |
		    BCM_BSC_CTRL_ST | BCM_BSC_CTRL_INT_ALL);

		if (!(sc->sc_curmsg->flags & IIC_M_RD)) {
			bcm_bsc_fill_tx_fifo(sc);
		}

		/* Wait for the transaction to complete. */
		while (err == 0 && !(sc->sc_flags & BCM_I2C_DONE)) {
			err = mtx_sleep(sc, &sc->sc_mtx, 0, "bsciow", hz);
		}
		/* Check for errors. */
		if (err == 0 && (sc->sc_flags & BCM_I2C_ERROR))
			err = EIO;
xfer_done:
		DEBUGF(sc, 1, " err=%d\n", err);
		DEVICE_DEBUGF(sc, 2, "stop\n");
		if (err != 0)
			break;
	}

	/* Disable interrupts, clean fifo, etc. */
	bcm_bsc_reset(sc);

	/* Clean the controller flags. */
	sc->sc_flags = 0;

	/* Wake up the threads waiting for bus. */
	wakeup(dev);

	BCM_BSC_UNLOCK(sc);

	return (err);
}

static int
bcm_bsc_iicbus_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr)
{
	struct bcm_bsc_softc *sc;
	uint32_t busfreq;

	sc = device_get_softc(dev);
	BCM_BSC_LOCK(sc);
	bcm_bsc_reset(sc);
	if (sc->sc_iicbus == NULL)
		busfreq = 100000;
	else
		busfreq = IICBUS_GET_FREQUENCY(sc->sc_iicbus, speed);
	BCM_BSC_WRITE(sc, BCM_BSC_CLOCK, BCM_BSC_CORE_CLK / busfreq);
	BCM_BSC_UNLOCK(sc);

	return (IIC_ENOADDR);
}

static phandle_t
bcm_bsc_get_node(device_t bus, device_t dev)
{

	/* We only have one child, the I2C bus, which needs our own node. */
	return (ofw_bus_get_node(bus));
}

static device_method_t bcm_bsc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bcm_bsc_probe),
	DEVMETHOD(device_attach,	bcm_bsc_attach),
	DEVMETHOD(device_detach,	bcm_bsc_detach),

	/* iicbus interface */
	DEVMETHOD(iicbus_reset,		bcm_bsc_iicbus_reset),
	DEVMETHOD(iicbus_callback,	iicbus_null_callback),
	DEVMETHOD(iicbus_transfer,	bcm_bsc_transfer),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	bcm_bsc_get_node),

	DEVMETHOD_END
};

static devclass_t bcm_bsc_devclass;

static driver_t bcm_bsc_driver = {
	"iichb",
	bcm_bsc_methods,
	sizeof(struct bcm_bsc_softc),
};

DRIVER_MODULE(iicbus, bcm2835_bsc, iicbus_driver, iicbus_devclass, 0, 0);
DRIVER_MODULE(bcm2835_bsc, simplebus, bcm_bsc_driver, bcm_bsc_devclass, 0, 0);
