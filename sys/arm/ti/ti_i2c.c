/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Ben Gray <ben.r.gray@gmail.com>.
 * Copyright (c) 2014 Luiz Otavio O Souza <loos@freebsd.org>.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * Driver for the I2C module on the TI SoC.
 *
 * This driver is heavily based on the TWI driver for the AT91 (at91_twi.c).
 *
 * CAUTION: The I2Ci registers are limited to 16 bit and 8 bit data accesses,
 * 32 bit data access is not allowed and can corrupt register content.
 *
 * This driver currently doesn't use DMA for the transfer, although I hope to
 * incorporate that sometime in the future.  The idea being that for transaction
 * larger than a certain size the DMA engine is used, for anything less the
 * normal interrupt/fifo driven option is used.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ti/ti_cpuid.h>
#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_hwmods.h>
#include <arm/ti/ti_i2c.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include "iicbus_if.h"

/**
 *	I2C device driver context, a pointer to this is stored in the device
 *	driver structure.
 */
struct ti_i2c_softc
{
	device_t		sc_dev;
	clk_ident_t		clk_id;
	struct resource*	sc_irq_res;
	struct resource*	sc_mem_res;
	device_t		sc_iicbus;

	void*			sc_irq_h;

	struct mtx		sc_mtx;

	struct iic_msg*		sc_buffer;
	int			sc_bus_inuse;
	int			sc_buffer_pos;
	int			sc_error;
	int			sc_fifo_trsh;
	int			sc_timeout;

	uint16_t		sc_con_reg;
	uint16_t		sc_rev;
};

struct ti_i2c_clock_config
{
	u_int   frequency;	/* Bus frequency in Hz */
	uint8_t psc;		/* Fast/Standard mode prescale divider */
	uint8_t scll;		/* Fast/Standard mode SCL low time */
	uint8_t sclh;		/* Fast/Standard mode SCL high time */
	uint8_t hsscll;		/* High Speed mode SCL low time */
	uint8_t hssclh;		/* High Speed mode SCL high time */
};

#if defined(SOC_OMAP4)
/*
 * OMAP4 i2c bus clock is 96MHz / ((psc + 1) * (scll + 7 + sclh + 5)).
 * The prescaler values for 100KHz and 400KHz modes come from the table in the
 * OMAP4 TRM.  The table doesn't list 1MHz; these values should give that speed.
 */
static struct ti_i2c_clock_config ti_omap4_i2c_clock_configs[] = {
	{  100000, 23,  13,  15,  0,  0},
	{  400000,  9,   5,   7,  0,  0},
	{ 1000000,  3,   5,   7,  0,  0},
/*	{ 3200000,  1, 113, 115,  7, 10}, - HS mode */
	{       0 /* Table terminator */ }
};
#endif

#if defined(SOC_TI_AM335X)
/*
 * AM335x i2c bus clock is 48MHZ / ((psc + 1) * (scll + 7 + sclh + 5))
 * In all cases we prescale the clock to 24MHz as recommended in the manual.
 */
static struct ti_i2c_clock_config ti_am335x_i2c_clock_configs[] = {
	{  100000, 1, 111, 117, 0, 0},
	{  400000, 1,  23,  25, 0, 0},
	{ 1000000, 1,   5,   7, 0, 0},
	{       0 /* Table terminator */ }
};
#endif

/**
 *	Locking macros used throughout the driver
 */
#define	TI_I2C_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	TI_I2C_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	TI_I2C_LOCK_INIT(_sc)						\
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->sc_dev),	\
	    "ti_i2c", MTX_DEF)
#define	TI_I2C_LOCK_DESTROY(_sc)	mtx_destroy(&_sc->sc_mtx)
#define	TI_I2C_ASSERT_LOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_OWNED)
#define	TI_I2C_ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_NOTOWNED)

#ifdef DEBUG
#define	ti_i2c_dbg(_sc, fmt, args...)					\
	device_printf((_sc)->sc_dev, fmt, ##args)
#else
#define	ti_i2c_dbg(_sc, fmt, args...)
#endif

/**
 *	ti_i2c_read_2 - reads a 16-bit value from one of the I2C registers
 *	@sc: I2C device context
 *	@off: the byte offset within the register bank to read from.
 *
 *
 *	LOCKING:
 *	No locking required
 *
 *	RETURNS:
 *	16-bit value read from the register.
 */
static inline uint16_t
ti_i2c_read_2(struct ti_i2c_softc *sc, bus_size_t off)
{

	return (bus_read_2(sc->sc_mem_res, off));
}

/**
 *	ti_i2c_write_2 - writes a 16-bit value to one of the I2C registers
 *	@sc: I2C device context
 *	@off: the byte offset within the register bank to read from.
 *	@val: the value to write into the register
 *
 *	LOCKING:
 *	No locking required
 *
 *	RETURNS:
 *	16-bit value read from the register.
 */
static inline void
ti_i2c_write_2(struct ti_i2c_softc *sc, bus_size_t off, uint16_t val)
{

	bus_write_2(sc->sc_mem_res, off, val);
}

static int
ti_i2c_transfer_intr(struct ti_i2c_softc* sc, uint16_t status)
{
	int amount, done, i;

	done = 0;
	amount = 0;
	/* Check for the error conditions. */
	if (status & I2C_STAT_NACK) {
		/* No ACK from slave. */
		ti_i2c_dbg(sc, "NACK\n");
		ti_i2c_write_2(sc, I2C_REG_STATUS, I2C_STAT_NACK);
		sc->sc_error = ENXIO;
	} else if (status & I2C_STAT_AL) {
		/* Arbitration lost. */
		ti_i2c_dbg(sc, "Arbitration lost\n");
		ti_i2c_write_2(sc, I2C_REG_STATUS, I2C_STAT_AL);
		sc->sc_error = ENXIO;
	}

	/* Check if we have finished. */
	if (status & I2C_STAT_ARDY) {
		/* Register access ready - transaction complete basically. */
		ti_i2c_dbg(sc, "ARDY transaction complete\n");
		if (sc->sc_error != 0 && sc->sc_buffer->flags & IIC_M_NOSTOP) {
			ti_i2c_write_2(sc, I2C_REG_CON,
			    sc->sc_con_reg | I2C_CON_STP);
		}
		ti_i2c_write_2(sc, I2C_REG_STATUS,
		    I2C_STAT_ARDY | I2C_STAT_RDR | I2C_STAT_RRDY |
		    I2C_STAT_XDR | I2C_STAT_XRDY);
		return (1);
	}

	if (sc->sc_buffer->flags & IIC_M_RD) {
		/* Read some data. */
		if (status & I2C_STAT_RDR) {
			/*
			 * Receive draining interrupt - last data received.
			 * The set FIFO threshold won't be reached to trigger
			 * RRDY.
			 */
			ti_i2c_dbg(sc, "Receive draining interrupt\n");

			/*
			 * Drain the FIFO.  Read the pending data in the FIFO.
			 */
			amount = sc->sc_buffer->len - sc->sc_buffer_pos;
		} else if (status & I2C_STAT_RRDY) {
			/*
			 * Receive data ready interrupt - FIFO has reached the
			 * set threshold.
			 */
			ti_i2c_dbg(sc, "Receive data ready interrupt\n");

			amount = min(sc->sc_fifo_trsh,
			    sc->sc_buffer->len - sc->sc_buffer_pos);
		}

		/* Read the bytes from the fifo. */
		for (i = 0; i < amount; i++)
			sc->sc_buffer->buf[sc->sc_buffer_pos++] = 
			    (uint8_t)(ti_i2c_read_2(sc, I2C_REG_DATA) & 0xff);

		if (status & I2C_STAT_RDR)
			ti_i2c_write_2(sc, I2C_REG_STATUS, I2C_STAT_RDR);
		if (status & I2C_STAT_RRDY)
			ti_i2c_write_2(sc, I2C_REG_STATUS, I2C_STAT_RRDY);

	} else {
		/* Write some data. */
		if (status & I2C_STAT_XDR) {
			/*
			 * Transmit draining interrupt - FIFO level is below
			 * the set threshold and the amount of data still to
			 * be transferred won't reach the set FIFO threshold.
			 */
			ti_i2c_dbg(sc, "Transmit draining interrupt\n");

			/*
			 * Drain the TX data.  Write the pending data in the
			 * FIFO.
			 */
			amount = sc->sc_buffer->len - sc->sc_buffer_pos;
		} else if (status & I2C_STAT_XRDY) {
			/*
			 * Transmit data ready interrupt - the FIFO level
			 * is below the set threshold.
			 */
			ti_i2c_dbg(sc, "Transmit data ready interrupt\n");

			amount = min(sc->sc_fifo_trsh,
			    sc->sc_buffer->len - sc->sc_buffer_pos);
		}

		/* Write the bytes from the fifo. */
		for (i = 0; i < amount; i++)
			ti_i2c_write_2(sc, I2C_REG_DATA,
			    sc->sc_buffer->buf[sc->sc_buffer_pos++]);

		if (status & I2C_STAT_XDR)
			ti_i2c_write_2(sc, I2C_REG_STATUS, I2C_STAT_XDR);
		if (status & I2C_STAT_XRDY)
			ti_i2c_write_2(sc, I2C_REG_STATUS, I2C_STAT_XRDY);
	}

	return (done);
}

/**
 *	ti_i2c_intr - interrupt handler for the I2C module
 *	@dev: i2c device handle
 *
 *
 *
 *	LOCKING:
 *	Called from timer context
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
static void
ti_i2c_intr(void *arg)
{
	int done;
	struct ti_i2c_softc *sc;
	uint16_t events, status;

 	sc = (struct ti_i2c_softc *)arg;

	TI_I2C_LOCK(sc);

	status = ti_i2c_read_2(sc, I2C_REG_STATUS);
	if (status == 0) {
		TI_I2C_UNLOCK(sc);
		return;
	}

	/* Save enabled interrupts. */
	events = ti_i2c_read_2(sc, I2C_REG_IRQENABLE_SET);

	/* We only care about enabled interrupts. */
	status &= events;

	done = 0;

	if (sc->sc_buffer != NULL)
		done = ti_i2c_transfer_intr(sc, status);
	else {
		ti_i2c_dbg(sc, "Transfer interrupt without buffer\n");
		sc->sc_error = EINVAL;
		done = 1;
	}

	if (done)
		/* Wakeup the process that started the transaction. */
		wakeup(sc);

	TI_I2C_UNLOCK(sc);
}

/**
 *	ti_i2c_transfer - called to perform the transfer
 *	@dev: i2c device handle
 *	@msgs: the messages to send/receive
 *	@nmsgs: the number of messages in the msgs array
 *
 *
 *	LOCKING:
 *	Internally locked
 *
 *	RETURNS:
 *	0 on function succeeded
 *	EINVAL if invalid message is passed as an arg
 */
static int
ti_i2c_transfer(device_t dev, struct iic_msg *msgs, uint32_t nmsgs)
{
	int err, i, repstart, timeout;
	struct ti_i2c_softc *sc;
	uint16_t reg;

 	sc = device_get_softc(dev);
	TI_I2C_LOCK(sc);

	/* If the controller is busy wait until it is available. */
	while (sc->sc_bus_inuse == 1)
		mtx_sleep(sc, &sc->sc_mtx, 0, "i2cbuswait", 0);

	/* Now we have control over the I2C controller. */
	sc->sc_bus_inuse = 1;

	err = 0;
	repstart = 0;
	for (i = 0; i < nmsgs; i++) {

		sc->sc_buffer = &msgs[i];
		sc->sc_buffer_pos = 0;
		sc->sc_error = 0;

		/* Zero byte transfers aren't allowed. */
		if (sc->sc_buffer == NULL || sc->sc_buffer->buf == NULL ||
		    sc->sc_buffer->len == 0) {
			err = EINVAL;
			break;
		}

		/* Check if the i2c bus is free. */
		if (repstart == 0) {
			/*
			 * On repeated start we send the START condition while
			 * the bus _is_ busy.
			 */
			timeout = 0;
			while (ti_i2c_read_2(sc, I2C_REG_STATUS_RAW) & I2C_STAT_BB) {
				if (timeout++ > 100) {
					err = EBUSY;
					goto out;
				}
				DELAY(1000);
			}
			timeout = 0;
		} else
			repstart = 0;

		if (sc->sc_buffer->flags & IIC_M_NOSTOP)
			repstart = 1;

		/* Set the slave address. */
		ti_i2c_write_2(sc, I2C_REG_SA, msgs[i].slave >> 1);

		/* Write the data length. */
		ti_i2c_write_2(sc, I2C_REG_CNT, sc->sc_buffer->len);

		/* Clear the RX and the TX FIFO. */
		reg = ti_i2c_read_2(sc, I2C_REG_BUF);
		reg |= I2C_BUF_RXFIFO_CLR | I2C_BUF_TXFIFO_CLR;
		ti_i2c_write_2(sc, I2C_REG_BUF, reg);

		reg = sc->sc_con_reg | I2C_CON_STT;
		if (repstart == 0)
			reg |= I2C_CON_STP;
		if ((sc->sc_buffer->flags & IIC_M_RD) == 0)
			reg |= I2C_CON_TRX;
		ti_i2c_write_2(sc, I2C_REG_CON, reg);

		/* Wait for an event. */
		err = mtx_sleep(sc, &sc->sc_mtx, 0, "i2ciowait", sc->sc_timeout);
		if (err == 0)
			err = sc->sc_error;

		if (err)
			break;
	}

out:
	if (timeout == 0) {
		while (ti_i2c_read_2(sc, I2C_REG_STATUS_RAW) & I2C_STAT_BB) {
			if (timeout++ > 100)
				break;
			DELAY(1000);
		}
	}
	/* Put the controller in master mode again. */
	if ((ti_i2c_read_2(sc, I2C_REG_CON) & I2C_CON_MST) == 0)
		ti_i2c_write_2(sc, I2C_REG_CON, sc->sc_con_reg);

	sc->sc_buffer = NULL;
	sc->sc_bus_inuse = 0;

	/* Wake up the processes that are waiting for the bus. */
	wakeup(sc);

	TI_I2C_UNLOCK(sc);

	return (err);
}

static int
ti_i2c_reset(struct ti_i2c_softc *sc, u_char speed)
{
	int timeout;
	struct ti_i2c_clock_config *clkcfg;
	u_int busfreq;
	uint16_t fifo_trsh, reg, scll, sclh;

	switch (ti_chip()) {
#ifdef SOC_OMAP4
	case CHIP_OMAP_4:
		clkcfg = ti_omap4_i2c_clock_configs;
		break;
#endif
#ifdef SOC_TI_AM335X
	case CHIP_AM335X:
		clkcfg = ti_am335x_i2c_clock_configs;
		break;
#endif
	default:
		panic("Unknown TI SoC, unable to reset the i2c");
	}

	/*
	 * If we haven't attached the bus yet, just init at the default slow
	 * speed.  This lets us get the hardware initialized enough to attach
	 * the bus which is where the real speed configuration is handled. After
	 * the bus is attached, get the configured speed from it.  Search the
	 * configuration table for the best speed we can do that doesn't exceed
	 * the requested speed.
	 */
	if (sc->sc_iicbus == NULL)
		busfreq = 100000;
	else
		busfreq = IICBUS_GET_FREQUENCY(sc->sc_iicbus, speed);
	for (;;) {
		if (clkcfg[1].frequency == 0 || clkcfg[1].frequency > busfreq)
			break;
		clkcfg++;
	}

	/*
	 * 23.1.4.3 - HS I2C Software Reset
	 *    From OMAP4 TRM at page 4068.
	 *
	 * 1. Ensure that the module is disabled.
	 */
	sc->sc_con_reg = 0;
	ti_i2c_write_2(sc, I2C_REG_CON, sc->sc_con_reg);

	/* 2. Issue a softreset to the controller. */
	bus_write_2(sc->sc_mem_res, I2C_REG_SYSC, I2C_REG_SYSC_SRST);

	/*
	 * 3. Enable the module.
	 *    The I2Ci.I2C_SYSS[0] RDONE bit is asserted only after the module
	 *    is enabled by setting the I2Ci.I2C_CON[15] I2C_EN bit to 1.
	 */
	ti_i2c_write_2(sc, I2C_REG_CON, I2C_CON_I2C_EN);

 	/* 4. Wait for the software reset to complete. */
	timeout = 0;
	while ((ti_i2c_read_2(sc, I2C_REG_SYSS) & I2C_SYSS_RDONE) == 0) {
		if (timeout++ > 100)
			return (EBUSY);
		DELAY(100);
	}

	/*
	 * Disable the I2C controller once again, now that the reset has
	 * finished.
	 */
	ti_i2c_write_2(sc, I2C_REG_CON, sc->sc_con_reg);

	/*
	 * The following sequence is taken from the OMAP4 TRM at page 4077.
	 *
	 * 1. Enable the functional and interface clocks (see Section
	 *    23.1.5.1.1.1.1).  Done at ti_i2c_activate().
	 *
	 * 2. Program the prescaler to obtain an approximately 12MHz internal
	 *    sampling clock (I2Ci_INTERNAL_CLK) by programming the
	 *    corresponding value in the I2Ci.I2C_PSC[3:0] PSC field.
	 *    This value depends on the frequency of the functional clock
	 *    (I2Ci_FCLK).  Because this frequency is 96MHz, the
	 *    I2Ci.I2C_PSC[7:0] PSC field value is 0x7.
	 */
	ti_i2c_write_2(sc, I2C_REG_PSC, clkcfg->psc);

	/*
	 * 3. Program the I2Ci.I2C_SCLL[7:0] SCLL and I2Ci.I2C_SCLH[7:0] SCLH
	 *    bit fields to obtain a bit rate of 100 Kbps, 400 Kbps or 1Mbps.
	 *    These values depend on the internal sampling clock frequency
	 *    (see Table 23-8).
	 */
	scll = clkcfg->scll & I2C_SCLL_MASK;
	sclh = clkcfg->sclh & I2C_SCLH_MASK;

	/*
	 * 4. (Optional) Program the I2Ci.I2C_SCLL[15:8] HSSCLL and
	 *    I2Ci.I2C_SCLH[15:8] HSSCLH fields to obtain a bit rate of
	 *    400K bps or 3.4M bps (for the second phase of HS mode).  These
	 *    values depend on the internal sampling clock frequency (see
	 *    Table 23-8).
	 *
	 * 5. (Optional) If a bit rate of 3.4M bps is used and the bus line
	 *    capacitance exceeds 45 pF, (see Section 18.4.8, PAD Functional
	 *    Multiplexing and Configuration).
	 */
	switch (ti_chip()) {
#ifdef SOC_OMAP4
	case CHIP_OMAP_4:
		if ((clkcfg->hsscll + clkcfg->hssclh) > 0) {
			scll |= clkcfg->hsscll << I2C_HSSCLL_SHIFT;
			sclh |= clkcfg->hssclh << I2C_HSSCLH_SHIFT;
			sc->sc_con_reg |= I2C_CON_OPMODE_HS;
		}
		break;
#endif
	}

	/* Write the selected bit rate. */
	ti_i2c_write_2(sc, I2C_REG_SCLL, scll);
	ti_i2c_write_2(sc, I2C_REG_SCLH, sclh);

	/*
	 * 6. Configure the Own Address of the I2C controller by storing it in
	 *    the I2Ci.I2C_OA0 register.  Up to four Own Addresses can be
	 *    programmed in the I2Ci.I2C_OAi registers (where i = 0, 1, 2, 3)
	 *    for each I2C controller.
	 *
	 * Note: For a 10-bit address, set the corresponding expand Own Address
	 * bit in the I2Ci.I2C_CON register.
	 *
	 * Driver currently always in single master mode so ignore this step.
	 */

	/*
	 * 7. Set the TX threshold (in transmitter mode) and the RX threshold
	 *    (in receiver mode) by setting the I2Ci.I2C_BUF[5:0]XTRSH field to
	 *    (TX threshold - 1) and the I2Ci.I2C_BUF[13:8]RTRSH field to (RX
	 *    threshold - 1), where the TX and RX thresholds are greater than
	 *    or equal to 1.
	 *
	 * The threshold is set to 5 for now.
	 */
	fifo_trsh = (sc->sc_fifo_trsh - 1) & I2C_BUF_TRSH_MASK;
	reg = fifo_trsh | (fifo_trsh << I2C_BUF_RXTRSH_SHIFT);
	ti_i2c_write_2(sc, I2C_REG_BUF, reg);

	/*
	 * 8. Take the I2C controller out of reset by setting the
	 *    I2Ci.I2C_CON[15] I2C_EN bit to 1.
	 *
	 * 23.1.5.1.1.1.2 - Initialize the I2C Controller
	 *
	 * To initialize the I2C controller, perform the following steps:
	 *
	 * 1. Configure the I2Ci.I2C_CON register:
	 *     . For master or slave mode, set the I2Ci.I2C_CON[10] MST bit
	 *       (0: slave, 1: master).
	 *     . For transmitter or receiver mode, set the I2Ci.I2C_CON[9] TRX
	 *       bit (0: receiver, 1: transmitter).
	 */

	/* Enable the I2C controller in master mode. */
	sc->sc_con_reg |= I2C_CON_I2C_EN | I2C_CON_MST;
	ti_i2c_write_2(sc, I2C_REG_CON, sc->sc_con_reg);

	/*
	 * 2. If using an interrupt to transmit/receive data, set the
	 *    corresponding bit in the I2Ci.I2C_IE register (the I2Ci.I2C_IE[4]
	 *    XRDY_IE bit for the transmit interrupt, the I2Ci.I2C_IE[3] RRDY
	 *    bit for the receive interrupt).
	 */

	/* Set the interrupts we want to be notified. */
	reg = I2C_IE_XDR |	/* Transmit draining interrupt. */
	    I2C_IE_XRDY |	/* Transmit Data Ready interrupt. */
	    I2C_IE_RDR |	/* Receive draining interrupt. */
	    I2C_IE_RRDY |	/* Receive Data Ready interrupt. */
	    I2C_IE_ARDY |	/* Register Access Ready interrupt. */
	    I2C_IE_NACK |	/* No Acknowledgment interrupt. */
	    I2C_IE_AL;		/* Arbitration lost interrupt. */

	/* Enable the interrupts. */
	ti_i2c_write_2(sc, I2C_REG_IRQENABLE_SET, reg);

	/*
	 * 3. If using DMA to receive/transmit data, set to 1 the corresponding
	 *    bit in the I2Ci.I2C_BUF register (the I2Ci.I2C_BUF[15] RDMA_EN
	 *    bit for the receive DMA channel, the I2Ci.I2C_BUF[7] XDMA_EN bit
	 *    for the transmit DMA channel).
	 *
	 * Not using DMA for now, so ignore this.
	 */

	return (0);
}

static int
ti_i2c_iicbus_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr)
{
	struct ti_i2c_softc *sc;
	int err;

	sc = device_get_softc(dev);
	TI_I2C_LOCK(sc);
	err = ti_i2c_reset(sc, speed);
	TI_I2C_UNLOCK(sc);
	if (err)
		return (err);

	return (IIC_ENOADDR);
}

static int
ti_i2c_activate(device_t dev)
{
	int err;
	struct ti_i2c_softc *sc;

	sc = (struct ti_i2c_softc*)device_get_softc(dev);

	/*
	 * 1. Enable the functional and interface clocks (see Section
	 * 23.1.5.1.1.1.1).
	 */
	err = ti_prcm_clk_enable(sc->clk_id);
	if (err)
		return (err);

	return (ti_i2c_reset(sc, IIC_UNKNOWN));
}

/**
 *	ti_i2c_deactivate - deactivates the controller and releases resources
 *	@dev: i2c device handle
 *
 *
 *
 *	LOCKING:
 *	Assumed called in an atomic context.
 *
 *	RETURNS:
 *	nothing
 */
static void
ti_i2c_deactivate(device_t dev)
{
	struct ti_i2c_softc *sc = device_get_softc(dev);

	/* Disable the controller - cancel all transactions. */
	ti_i2c_write_2(sc, I2C_REG_IRQENABLE_CLR, 0xffff);
	ti_i2c_write_2(sc, I2C_REG_STATUS, 0xffff);
	ti_i2c_write_2(sc, I2C_REG_CON, 0);

	/* Release the interrupt handler. */
	if (sc->sc_irq_h != NULL) {
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_irq_h);
		sc->sc_irq_h = NULL;
	}

	bus_generic_detach(sc->sc_dev);

	/* Unmap the I2C controller registers. */
	if (sc->sc_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		sc->sc_mem_res = NULL;
	}

	/* Release the IRQ resource. */
	if (sc->sc_irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
		sc->sc_irq_res = NULL;
	}

	/* Finally disable the functional and interface clocks. */
	ti_prcm_clk_disable(sc->clk_id);
}

static int
ti_i2c_sysctl_clk(SYSCTL_HANDLER_ARGS)
{
	int clk, psc, sclh, scll;
	struct ti_i2c_softc *sc;

	sc = arg1;

	TI_I2C_LOCK(sc);
	/* Get the system prescaler value. */
	psc = (int)ti_i2c_read_2(sc, I2C_REG_PSC) + 1;

	/* Get the bitrate. */
	scll = (int)ti_i2c_read_2(sc, I2C_REG_SCLL) & I2C_SCLL_MASK;
	sclh = (int)ti_i2c_read_2(sc, I2C_REG_SCLH) & I2C_SCLH_MASK;

	clk = I2C_CLK / psc / (scll + 7 + sclh + 5);
	TI_I2C_UNLOCK(sc);

	return (sysctl_handle_int(oidp, &clk, 0, req));
}

static int
ti_i2c_sysctl_timeout(SYSCTL_HANDLER_ARGS)
{
	struct ti_i2c_softc *sc;
	unsigned int val;
	int err;

	sc = arg1;

	/* 
	 * MTX_DEF lock can't be held while doing uimove in
	 * sysctl_handle_int
	 */
	TI_I2C_LOCK(sc);
	val = sc->sc_timeout;
	TI_I2C_UNLOCK(sc);

	err = sysctl_handle_int(oidp, &val, 0, req);
	/* Write request? */
	if ((err == 0) && (req->newptr != NULL)) {
		TI_I2C_LOCK(sc);
		sc->sc_timeout = val;
		TI_I2C_UNLOCK(sc);
	}

	return (err);
}

static int
ti_i2c_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (!ofw_bus_is_compatible(dev, "ti,omap4-i2c"))
		return (ENXIO);
	device_set_desc(dev, "TI I2C Controller");

	return (0);
}

static int
ti_i2c_attach(device_t dev)
{
	int err, rid;
	phandle_t node;
	struct ti_i2c_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *tree;
	uint16_t fifosz;

 	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	/* Get the i2c device id from FDT. */
	node = ofw_bus_get_node(dev);
	/* i2c ti,hwmods bindings is special: it start with index 1 */
	sc->clk_id = ti_hwmods_get_clock(dev);
	if (sc->clk_id == INVALID_CLK_IDENT) {
		device_printf(dev, "failed to get device id using ti,hwmod\n");
		return (ENXIO);
	}

	/* Get the memory resource for the register mapping. */
	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "Cannot map registers.\n");
		return (ENXIO);
	}

	/* Allocate our IRQ resource. */
	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_irq_res == NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		device_printf(dev, "Cannot allocate interrupt.\n");
		return (ENXIO);
	}

	TI_I2C_LOCK_INIT(sc);

	/* First of all, we _must_ activate the H/W. */
	err = ti_i2c_activate(dev);
	if (err) {
		device_printf(dev, "ti_i2c_activate failed\n");
		goto out;
	}

	/* Read the version number of the I2C module */
	sc->sc_rev = ti_i2c_read_2(sc, I2C_REG_REVNB_HI) & 0xff;

	/* Get the fifo size. */
	fifosz = ti_i2c_read_2(sc, I2C_REG_BUFSTAT);
	fifosz >>= I2C_BUFSTAT_FIFODEPTH_SHIFT;
	fifosz &= I2C_BUFSTAT_FIFODEPTH_MASK;

	device_printf(dev, "I2C revision %d.%d FIFO size: %d bytes\n",
	    sc->sc_rev >> 4, sc->sc_rev & 0xf, 8 << fifosz);

	/* Set the FIFO threshold to 5 for now. */
	sc->sc_fifo_trsh = 5;

	/* Set I2C bus timeout */
	sc->sc_timeout = 5*hz;

	ctx = device_get_sysctl_ctx(dev);
	tree = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "i2c_clock",
	    CTLFLAG_RD | CTLTYPE_UINT | CTLFLAG_MPSAFE, sc, 0,
	    ti_i2c_sysctl_clk, "IU", "I2C bus clock");

	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "i2c_timeout",
	    CTLFLAG_RW | CTLTYPE_UINT | CTLFLAG_MPSAFE, sc, 0,
	    ti_i2c_sysctl_timeout, "IU", "I2C bus timeout (in ticks)");

	/* Activate the interrupt. */
	err = bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, ti_i2c_intr, sc, &sc->sc_irq_h);
	if (err)
		goto out;

	/* Attach the iicbus. */
	if ((sc->sc_iicbus = device_add_child(dev, "iicbus", -1)) == NULL) {
		device_printf(dev, "could not allocate iicbus instance\n");
		err = ENXIO;
		goto out;
	}

	/* Probe and attach the iicbus when interrupts are available. */
	config_intrhook_oneshot((ich_func_t)bus_generic_attach, dev);

out:
	if (err) {
		ti_i2c_deactivate(dev);
		TI_I2C_LOCK_DESTROY(sc);
	}

	return (err);
}

static int
ti_i2c_detach(device_t dev)
{
	struct ti_i2c_softc *sc;
	int rv;

 	sc = device_get_softc(dev);
	ti_i2c_deactivate(dev);
	TI_I2C_LOCK_DESTROY(sc);
	if (sc->sc_iicbus &&
	    (rv = device_delete_child(dev, sc->sc_iicbus)) != 0)
		return (rv);

	return (0);
}

static phandle_t
ti_i2c_get_node(device_t bus, device_t dev)
{

	/* Share controller node with iibus device. */
	return (ofw_bus_get_node(bus));
}

static device_method_t ti_i2c_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ti_i2c_probe),
	DEVMETHOD(device_attach,	ti_i2c_attach),
	DEVMETHOD(device_detach,	ti_i2c_detach),

	/* Bus interface */
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_alloc_resource,	bus_generic_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,	bus_generic_adjust_resource),
	DEVMETHOD(bus_set_resource,	bus_generic_rl_set_resource),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),

	/* OFW methods */
	DEVMETHOD(ofw_bus_get_node,	ti_i2c_get_node),

	/* iicbus interface */
	DEVMETHOD(iicbus_callback,	iicbus_null_callback),
	DEVMETHOD(iicbus_reset,		ti_i2c_iicbus_reset),
	DEVMETHOD(iicbus_transfer,	ti_i2c_transfer),

	DEVMETHOD_END
};

static driver_t ti_i2c_driver = {
	"iichb",
	ti_i2c_methods,
	sizeof(struct ti_i2c_softc),
};

static devclass_t ti_i2c_devclass;

DRIVER_MODULE(ti_iic, simplebus, ti_i2c_driver, ti_i2c_devclass, 0, 0);
DRIVER_MODULE(iicbus, ti_iic, iicbus_driver, iicbus_devclass, 0, 0);

MODULE_DEPEND(ti_iic, ti_prcm, 1, 1, 1);
MODULE_DEPEND(ti_iic, iicbus, 1, 1, 1);
