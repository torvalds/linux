/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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
 * I2C driver for Tegra SoCs.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <sys/lock.h>
#include <sys/mutex.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "iicbus_if.h"

#define	I2C_CNFG				0x000
#define	 I2C_CNFG_MSTR_CLR_BUS_ON_TIMEOUT		(1 << 15)
#define	 I2C_CNFG_DEBOUNCE_CNT(x)			(((x) & 0x07) << 12)
#define	 I2C_CNFG_NEW_MASTER_FSM			(1 << 11)
#define	 I2C_CNFG_PACKET_MODE_EN			(1 << 10)
#define	 I2C_CNFG_SEND					(1 <<  9)
#define	 I2C_CNFG_NOACK					(1 <<  8)
#define	 I2C_CNFG_CMD2					(1 <<  7)
#define	 I2C_CNFG_CMD1					(1 <<  6)
#define	 I2C_CNFG_START					(1 <<  5)
#define	 I2C_CNFG_SLV2					(1 <<  4)
#define	 I2C_CNFG_LENGTH_SHIFT				1
#define	 I2C_CNFG_LENGTH_MASK				0x7
#define	 I2C_CNFG_A_MOD					(1 <<  0)

#define	I2C_CMD_ADDR0				0x004
#define	I2C_CMD_ADDR1				0x008
#define	I2C_CMD_DATA1				0x00c
#define	I2C_CMD_DATA2				0x010
#define	I2C_STATUS				0x01c
#define	I2C_SL_CNFG				0x020
#define	I2C_SL_RCVD				0x024
#define	I2C_SL_STATUS				0x028
#define	I2C_SL_ADDR1				0x02c
#define	I2C_SL_ADDR2				0x030
#define	I2C_TLOW_SEXT				0x034
#define	I2C_SL_DELAY_COUNT			0x03c
#define	I2C_SL_INT_MASK				0x040
#define	I2C_SL_INT_SOURCE			0x044
#define	I2C_SL_INT_SET				0x048
#define	I2C_TX_PACKET_FIFO			0x050
#define	I2C_RX_FIFO				0x054
#define	I2C_PACKET_TRANSFER_STATUS		0x058
#define	I2C_FIFO_CONTROL			0x05c
#define	 I2C_FIFO_CONTROL_SLV_TX_FIFO_TRIG(x)		(((x) & 0x07) << 13)
#define	 I2C_FIFO_CONTROL_SLV_RX_FIFO_TRIG(x)		(((x) & 0x07) << 10)
#define	 I2C_FIFO_CONTROL_SLV_TX_FIFO_FLUSH		(1 <<  9)
#define	 I2C_FIFO_CONTROL_SLV_RX_FIFO_FLUSH		(1 <<  8)
#define	 I2C_FIFO_CONTROL_TX_FIFO_TRIG(x)		(((x) & 0x07) << 5)
#define	 I2C_FIFO_CONTROL_RX_FIFO_TRIG(x)		(((x) & 0x07) << 2)
#define	 I2C_FIFO_CONTROL_TX_FIFO_FLUSH			(1 << 1)
#define	 I2C_FIFO_CONTROL_RX_FIFO_FLUSH			(1 << 0)

#define	I2C_FIFO_STATUS				0x060
#define	 I2C_FIFO_STATUS_SLV_XFER_ERR_REASON		(1 << 25)
#define	 I2C_FIFO_STATUS_TX_FIFO_SLV_EMPTY_CNT(x)	(((x) >> 20) & 0xF)
#define	 I2C_FIFO_STATUS_RX_FIFO_SLV_FULL_CNT(x)	(((x) >> 16) & 0xF)
#define	 I2C_FIFO_STATUS_TX_FIFO_EMPTY_CNT(x)		(((x) >>  4) & 0xF)
#define	 I2C_FIFO_STATUS_RX_FIFO_FULL_CNT(x)		(((x) >>  0) & 0xF)

#define	I2C_INTERRUPT_MASK_REGISTER		0x064
#define	I2C_INTERRUPT_STATUS_REGISTER		0x068
#define	 I2C_INT_SLV_ACK_WITHHELD			(1 << 28)
#define	 I2C_INT_SLV_RD2WR				(1 << 27)
#define	 I2C_INT_SLV_WR2RD				(1 << 26)
#define	 I2C_INT_SLV_PKT_XFER_ERR			(1 << 25)
#define	 I2C_INT_SLV_TX_BUFFER_REQ			(1 << 24)
#define	 I2C_INT_SLV_RX_BUFFER_FILLED			(1 << 23)
#define	 I2C_INT_SLV_PACKET_XFER_COMPLETE		(1 << 22)
#define	 I2C_INT_SLV_TFIFO_OVF				(1 << 21)
#define	 I2C_INT_SLV_RFIFO_UNF				(1 << 20)
#define	 I2C_INT_SLV_TFIFO_DATA_REQ			(1 << 17)
#define	 I2C_INT_SLV_RFIFO_DATA_REQ			(1 << 16)
#define	 I2C_INT_BUS_CLEAR_DONE				(1 << 11)
#define	 I2C_INT_TLOW_MEXT_TIMEOUT			(1 << 10)
#define	 I2C_INT_TLOW_SEXT_TIMEOUT			(1 <<  9)
#define	 I2C_INT_TIMEOUT				(1 <<  8)
#define	 I2C_INT_PACKET_XFER_COMPLETE			(1 <<  7)
#define	 I2C_INT_ALL_PACKETS_XFER_COMPLETE		(1 <<  6)
#define	 I2C_INT_TFIFO_OVR				(1 <<  5)
#define	 I2C_INT_RFIFO_UNF				(1 <<  4)
#define	 I2C_INT_NOACK					(1 <<  3)
#define	 I2C_INT_ARB_LOST				(1 <<  2)
#define	 I2C_INT_TFIFO_DATA_REQ				(1 <<  1)
#define	 I2C_INT_RFIFO_DATA_REQ				(1 <<  0)
#define	 I2C_ERROR_MASK		(I2C_INT_ARB_LOST | I2C_INT_NOACK |	 \
				I2C_INT_RFIFO_UNF | I2C_INT_TFIFO_OVR)

#define	I2C_CLK_DIVISOR				0x06c
#define	 I2C_CLK_DIVISOR_STD_FAST_MODE_SHIFT		16
#define	 I2C_CLK_DIVISOR_STD_FAST_MODE_MASK		0xffff
#define	 I2C_CLK_DIVISOR_HSMODE_SHIFT			0
#define	 I2C_CLK_DIVISOR_HSMODE_MASK			0xffff
#define	I2C_INTERRUPT_SOURCE_REGISTER		0x070
#define	I2C_INTERRUPT_SET_REGISTER		0x074
#define	I2C_SLV_TX_PACKET_FIFO			0x07c
#define	I2C_SLV_PACKET_STATUS			0x080
#define	I2C_BUS_CLEAR_CONFIG			0x084
#define	 I2C_BUS_CLEAR_CONFIG_BC_SCLK_THRESHOLD(x)	(((x) & 0xFF) << 16)
#define	 I2C_BUS_CLEAR_CONFIG_BC_STOP_COND		(1 << 2)
#define	 I2C_BUS_CLEAR_CONFIG_BC_TERMINATE		(1 << 1)
#define	 I2C_BUS_CLEAR_CONFIG_BC_ENABLE			(1 << 0)

#define	I2C_BUS_CLEAR_STATUS			0x088
#define	 I2C_BUS_CLEAR_STATUS_BC_STATUS			(1 << 0)

#define	I2C_CONFIG_LOAD				0x08c
#define	 I2C_CONFIG_LOAD_TIMEOUT_CONFIG_LOAD		(1 << 2)
#define	 I2C_CONFIG_LOAD_SLV_CONFIG_LOAD		(1 << 1)
#define	 I2C_CONFIG_LOAD_MSTR_CONFIG_LOAD		(1 << 0)

#define	I2C_INTERFACE_TIMING_0			0x094
#define	I2C_INTERFACE_TIMING_1			0x098
#define	I2C_HS_INTERFACE_TIMING_0		0x09c
#define	I2C_HS_INTERFACE_TIMING_1		0x0a0

/* Protocol header 0 */
#define	PACKET_HEADER0_HEADER_SIZE_SHIFT	28
#define	PACKET_HEADER0_HEADER_SIZE_MASK		0x3
#define	PACKET_HEADER0_PACKET_ID_SHIFT		16
#define	PACKET_HEADER0_PACKET_ID_MASK		0xff
#define	PACKET_HEADER0_CONT_ID_SHIFT		12
#define	PACKET_HEADER0_CONT_ID_MASK		0xf
#define	PACKET_HEADER0_PROTOCOL_I2C		(1 << 4)
#define	PACKET_HEADER0_TYPE_SHIFT		0
#define	PACKET_HEADER0_TYPE_MASK		0x7

/* I2C header */
#define	I2C_HEADER_HIGHSPEED_MODE		(1 << 22)
#define	I2C_HEADER_CONT_ON_NAK			(1 << 21)
#define	I2C_HEADER_SEND_START_BYTE		(1 << 20)
#define	I2C_HEADER_READ				(1 << 19)
#define	I2C_HEADER_10BIT_ADDR			(1 << 18)
#define	I2C_HEADER_IE_ENABLE			(1 << 17)
#define	I2C_HEADER_REPEAT_START			(1 << 16)
#define	I2C_HEADER_CONTINUE_XFER		(1 << 15)
#define	I2C_HEADER_MASTER_ADDR_SHIFT		12
#define	I2C_HEADER_MASTER_ADDR_MASK		0x7
#define	I2C_HEADER_SLAVE_ADDR_SHIFT		0
#define	I2C_HEADER_SLAVE_ADDR_MASK		0x3ff

#define	I2C_CLK_DIVISOR_STD_FAST_MODE		0x19
#define	I2C_CLK_MULTIPLIER_STD_FAST_MODE	8

#define	I2C_REQUEST_TIMEOUT			(5 * hz)

#define	WR4(_sc, _r, _v)	bus_write_4((_sc)->mem_res, (_r), (_v))
#define	RD4(_sc, _r)		bus_read_4((_sc)->mem_res, (_r))

#define	LOCK(_sc)		mtx_lock(&(_sc)->mtx)
#define	UNLOCK(_sc)		mtx_unlock(&(_sc)->mtx)
#define	SLEEP(_sc, timeout)						\
	mtx_sleep(sc, &sc->mtx, 0, "i2cbuswait", timeout);
#define	LOCK_INIT(_sc)							\
	mtx_init(&_sc->mtx, device_get_nameunit(_sc->dev), "tegra_i2c", MTX_DEF)
#define	LOCK_DESTROY(_sc)	mtx_destroy(&_sc->mtx)
#define	ASSERT_LOCKED(_sc)	mtx_assert(&_sc->mtx, MA_OWNED)
#define	ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->mtx, MA_NOTOWNED)

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-i2c",	1},
	{NULL,			0}
};
enum tegra_i2c_xfer_type {
	XFER_STOP, 		/* Send stop condition after xfer */
	XFER_REPEAT_START,	/* Send repeated start after xfer */
	XFER_CONTINUE		/* Don't send nothing */
} ;

struct tegra_i2c_softc {
	device_t		dev;
	struct mtx		mtx;

	struct resource		*mem_res;
	struct resource		*irq_res;
	void			*irq_h;

	device_t		iicbus;
	clk_t			clk;
	hwreset_t		reset;
	uint32_t		core_freq;
	uint32_t		bus_freq;
	int			bus_inuse;

	struct iic_msg		*msg;
	int			msg_idx;
	uint32_t		bus_err;
	int			done;
};

static int
tegra_i2c_flush_fifo(struct tegra_i2c_softc *sc)
{
	int timeout;
	uint32_t reg;

	reg = RD4(sc, I2C_FIFO_CONTROL);
	reg |= I2C_FIFO_CONTROL_TX_FIFO_FLUSH | I2C_FIFO_CONTROL_RX_FIFO_FLUSH;
	WR4(sc, I2C_FIFO_CONTROL, reg);

	timeout = 10;
	while (timeout > 0) {
		reg = RD4(sc, I2C_FIFO_CONTROL);
		reg &= I2C_FIFO_CONTROL_TX_FIFO_FLUSH |
		    I2C_FIFO_CONTROL_RX_FIFO_FLUSH;
		if (reg == 0)
			break;
		DELAY(10);
	}
	if (timeout <= 0) {
		device_printf(sc->dev, "FIFO flush timedout\n");
		return (ETIMEDOUT);
	}
	return (0);
}

static void
tegra_i2c_setup_clk(struct tegra_i2c_softc *sc, int clk_freq)
{
	int div;

	div = ((sc->core_freq  / clk_freq)  / 10) - 1;
	if ((sc->core_freq / (10 * (div + 1)))  > clk_freq)
		div++;
	if (div > 65535)
		div = 65535;
	WR4(sc, I2C_CLK_DIVISOR,
	    (1 << I2C_CLK_DIVISOR_HSMODE_SHIFT) |
	    (div << I2C_CLK_DIVISOR_STD_FAST_MODE_SHIFT));
}

static void
tegra_i2c_bus_clear(struct tegra_i2c_softc *sc)
{
	int timeout;
	uint32_t reg, status;

	WR4(sc, I2C_BUS_CLEAR_CONFIG,
	    I2C_BUS_CLEAR_CONFIG_BC_SCLK_THRESHOLD(18) |
	    I2C_BUS_CLEAR_CONFIG_BC_STOP_COND |
	    I2C_BUS_CLEAR_CONFIG_BC_TERMINATE);

	WR4(sc, I2C_CONFIG_LOAD, I2C_CONFIG_LOAD_MSTR_CONFIG_LOAD);
	for (timeout = 1000; timeout > 0; timeout--) {
		if (RD4(sc, I2C_CONFIG_LOAD) == 0)
			break;
		DELAY(10);
	}
	if (timeout <= 0)
		device_printf(sc->dev, "config load timeouted\n");
	reg = RD4(sc, I2C_BUS_CLEAR_CONFIG);
	reg |= I2C_BUS_CLEAR_CONFIG_BC_ENABLE;
	WR4(sc, I2C_BUS_CLEAR_CONFIG,reg);

	for (timeout = 1000; timeout > 0; timeout--) {
		if ((RD4(sc, I2C_BUS_CLEAR_CONFIG) &
		    I2C_BUS_CLEAR_CONFIG_BC_ENABLE) == 0)
			break;
		DELAY(10);
	}
	if (timeout <= 0)
		device_printf(sc->dev, "bus clear timeouted\n");

	status = RD4(sc, I2C_BUS_CLEAR_STATUS);
	if ((status & I2C_BUS_CLEAR_STATUS_BC_STATUS) == 0)
		device_printf(sc->dev, "bus clear failed\n");
}

static int
tegra_i2c_hw_init(struct tegra_i2c_softc *sc)
{
	int rv, timeout;

	/* Reset the core. */
	rv = hwreset_assert(sc->reset);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot assert reset\n");
		return (rv);
	}
	DELAY(10);
	rv = hwreset_deassert(sc->reset);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot clear reset\n");
		return (rv);
	}

	WR4(sc, I2C_INTERRUPT_MASK_REGISTER, 0);
	WR4(sc, I2C_INTERRUPT_STATUS_REGISTER, 0xFFFFFFFF);
	WR4(sc, I2C_CNFG, I2C_CNFG_NEW_MASTER_FSM | I2C_CNFG_PACKET_MODE_EN |
	    I2C_CNFG_DEBOUNCE_CNT(2));

	tegra_i2c_setup_clk(sc, sc->bus_freq);

	WR4(sc, I2C_FIFO_CONTROL, I2C_FIFO_CONTROL_TX_FIFO_TRIG(7) |
	    I2C_FIFO_CONTROL_RX_FIFO_TRIG(0));

	WR4(sc, I2C_CONFIG_LOAD, I2C_CONFIG_LOAD_MSTR_CONFIG_LOAD);
	for (timeout = 1000; timeout > 0; timeout--) {
		if (RD4(sc, I2C_CONFIG_LOAD) == 0)
			break;
		DELAY(10);
	}
	if (timeout <= 0)
		device_printf(sc->dev, "config load timeouted\n");

	tegra_i2c_bus_clear(sc);
	return (0);
}

static int
tegra_i2c_tx(struct tegra_i2c_softc *sc)
{
	uint32_t reg;
	int cnt, i;

	if (sc->msg_idx >= sc->msg->len)
		panic("Invalid call to tegra_i2c_tx\n");

	while(sc->msg_idx < sc->msg->len) {
		reg = RD4(sc, I2C_FIFO_STATUS);
		if (I2C_FIFO_STATUS_TX_FIFO_EMPTY_CNT(reg) == 0)
			break;
		cnt = min(4, sc->msg->len - sc->msg_idx);
		reg = 0;
		for (i = 0;  i < cnt; i++) {
			reg |=  sc->msg->buf[sc->msg_idx] << (i * 8);
			sc->msg_idx++;
		}
		WR4(sc, I2C_TX_PACKET_FIFO, reg);
	}
	if (sc->msg_idx >= sc->msg->len)
		return (0);
	return (sc->msg->len - sc->msg_idx - 1);
}

static int
tegra_i2c_rx(struct tegra_i2c_softc *sc)
{
	uint32_t reg;
	int cnt, i;

	if (sc->msg_idx >= sc->msg->len)
		panic("Invalid call to tegra_i2c_rx\n");

	while(sc->msg_idx < sc->msg->len) {
		reg = RD4(sc, I2C_FIFO_STATUS);
		if (I2C_FIFO_STATUS_RX_FIFO_FULL_CNT(reg) == 0)
			break;
		cnt = min(4, sc->msg->len - sc->msg_idx);
		reg = RD4(sc, I2C_RX_FIFO);
		for (i = 0;  i < cnt; i++) {
			sc->msg->buf[sc->msg_idx] = (reg >> (i * 8)) & 0xFF;
			sc->msg_idx++;
		}
	}

	if (sc->msg_idx >= sc->msg->len)
		return (0);
	return (sc->msg->len - sc->msg_idx - 1);
}

static void
tegra_i2c_intr(void *arg)
{
	struct tegra_i2c_softc *sc;
	uint32_t status, reg;
	int rv;

	sc = (struct tegra_i2c_softc *)arg;

	LOCK(sc);
	status = RD4(sc, I2C_INTERRUPT_SOURCE_REGISTER);
	if (sc->msg == NULL) {
		/* Unexpected interrupt - disable FIFOs, clear reset. */
		reg = RD4(sc, I2C_INTERRUPT_MASK_REGISTER);
		reg &= ~I2C_INT_TFIFO_DATA_REQ;
		reg &= ~I2C_INT_RFIFO_DATA_REQ;
		WR4(sc, I2C_INTERRUPT_MASK_REGISTER, 0);
		WR4(sc, I2C_INTERRUPT_STATUS_REGISTER, status);
		UNLOCK(sc);
		return;
	}

	if ((status & I2C_ERROR_MASK) != 0) {
		if (status & I2C_INT_NOACK)
			sc->bus_err = IIC_ENOACK;
		if (status & I2C_INT_ARB_LOST)
			sc->bus_err = IIC_EBUSERR;
		if ((status & I2C_INT_TFIFO_OVR) ||
		    (status & I2C_INT_RFIFO_UNF))
			sc->bus_err = IIC_EBUSERR;
		sc->done = 1;
	} else if ((status & I2C_INT_RFIFO_DATA_REQ) &&
	    (sc->msg != NULL) && (sc->msg->flags & IIC_M_RD)) {
		rv = tegra_i2c_rx(sc);
		if (rv == 0) {
			reg = RD4(sc, I2C_INTERRUPT_MASK_REGISTER);
			reg &= ~I2C_INT_RFIFO_DATA_REQ;
			WR4(sc, I2C_INTERRUPT_MASK_REGISTER, reg);
		}
	} else if ((status & I2C_INT_TFIFO_DATA_REQ) &&
	    (sc->msg != NULL) && !(sc->msg->flags & IIC_M_RD)) {
		rv = tegra_i2c_tx(sc);
		if (rv == 0) {
			reg = RD4(sc, I2C_INTERRUPT_MASK_REGISTER);
			reg &= ~I2C_INT_TFIFO_DATA_REQ;
			WR4(sc, I2C_INTERRUPT_MASK_REGISTER, reg);
		}
	} else if ((status & I2C_INT_RFIFO_DATA_REQ) ||
		    (status & I2C_INT_TFIFO_DATA_REQ)) {
		device_printf(sc->dev, "Unexpected data interrupt: 0x%08X\n",
		    status);
		reg = RD4(sc, I2C_INTERRUPT_MASK_REGISTER);
		reg &= ~I2C_INT_TFIFO_DATA_REQ;
		reg &= ~I2C_INT_RFIFO_DATA_REQ;
		WR4(sc, I2C_INTERRUPT_MASK_REGISTER, reg);
	}
	if (status & I2C_INT_PACKET_XFER_COMPLETE)
		sc->done = 1;
	WR4(sc, I2C_INTERRUPT_STATUS_REGISTER, status);
	if (sc->done) {
		WR4(sc, I2C_INTERRUPT_MASK_REGISTER, 0);
		wakeup(&(sc->done));
	}
	UNLOCK(sc);
}

static void
tegra_i2c_start_msg(struct tegra_i2c_softc *sc, struct iic_msg *msg,
    enum tegra_i2c_xfer_type xtype)
{
	uint32_t tmp, mask;

	/* Packet header. */
	tmp =  (0 << PACKET_HEADER0_HEADER_SIZE_SHIFT) |
	   PACKET_HEADER0_PROTOCOL_I2C |
	   (1 << PACKET_HEADER0_CONT_ID_SHIFT) |
	   (1 << PACKET_HEADER0_PACKET_ID_SHIFT);
	WR4(sc, I2C_TX_PACKET_FIFO, tmp);


	/* Packet size. */
	WR4(sc, I2C_TX_PACKET_FIFO, msg->len - 1);

	/* I2C header. */
	tmp = I2C_HEADER_IE_ENABLE;
	if (xtype == XFER_CONTINUE)
		tmp |= I2C_HEADER_CONTINUE_XFER;
	else if (xtype == XFER_REPEAT_START)
		tmp |= I2C_HEADER_REPEAT_START;
	tmp |= msg->slave << I2C_HEADER_SLAVE_ADDR_SHIFT;
	if (msg->flags & IIC_M_RD) {
		tmp |= I2C_HEADER_READ;
		tmp |= 1 << I2C_HEADER_SLAVE_ADDR_SHIFT;
	} else
		tmp &= ~(1 << I2C_HEADER_SLAVE_ADDR_SHIFT);

	WR4(sc, I2C_TX_PACKET_FIFO, tmp);

	/* Interrupt mask. */
	mask = I2C_INT_NOACK | I2C_INT_ARB_LOST | I2C_INT_PACKET_XFER_COMPLETE;
	if (msg->flags & IIC_M_RD)
		mask |= I2C_INT_RFIFO_DATA_REQ;
	else
		mask |= I2C_INT_TFIFO_DATA_REQ;
	WR4(sc, I2C_INTERRUPT_MASK_REGISTER, mask);
}

static int
tegra_i2c_poll(struct tegra_i2c_softc *sc)
{
	int timeout;

	for(timeout = 10000; timeout > 0; timeout--)  {
		UNLOCK(sc);
		tegra_i2c_intr(sc);
		LOCK(sc);
		if (sc->done != 0)
			 break;
		DELAY(1);
	}
	if (timeout <= 0)
		return (ETIMEDOUT);
	return (0);
}

static int
tegra_i2c_transfer(device_t dev, struct iic_msg *msgs, uint32_t nmsgs)
{
	int rv, i;
	struct tegra_i2c_softc *sc;
	enum tegra_i2c_xfer_type xtype;

	sc = device_get_softc(dev);
	LOCK(sc);

	/* Get the bus. */
	while (sc->bus_inuse == 1)
		SLEEP(sc,  0);
	sc->bus_inuse = 1;

	rv = 0;
	for (i = 0; i < nmsgs; i++) {
		sc->msg = &msgs[i];
		sc->msg_idx = 0;
		sc->bus_err = 0;
		sc->done = 0;
		/* Check for valid parameters. */
		if (sc->msg == NULL || sc->msg->buf == NULL ||
		    sc->msg->len == 0) {
			rv = EINVAL;
			break;
		}

		/* Get flags for next transfer. */
		if (i == (nmsgs - 1)) {
			if (msgs[i].flags & IIC_M_NOSTOP)
				xtype = XFER_CONTINUE;
			else
				xtype = XFER_STOP;
		} else {
			if (msgs[i + 1].flags & IIC_M_NOSTART)
				xtype = XFER_CONTINUE;
			else
				xtype = XFER_REPEAT_START;
		}
		tegra_i2c_start_msg(sc, sc->msg, xtype);
		if (cold)
			rv = tegra_i2c_poll(sc);
		else
			rv = msleep(&sc->done, &sc->mtx, PZERO, "iic",
			    I2C_REQUEST_TIMEOUT);

		WR4(sc, I2C_INTERRUPT_MASK_REGISTER, 0);
		WR4(sc, I2C_INTERRUPT_STATUS_REGISTER, 0xFFFFFFFF);
		if (rv == 0)
			rv = sc->bus_err;
		if (rv != 0)
			break;
	}

	if (rv != 0) {
		tegra_i2c_hw_init(sc);
		tegra_i2c_flush_fifo(sc);
	}

	sc->msg = NULL;
	sc->msg_idx = 0;
	sc->bus_err = 0;
	sc->done = 0;

	/* Wake up the processes that are waiting for the bus. */
	sc->bus_inuse = 0;
	wakeup(sc);
	UNLOCK(sc);

	return (rv);
}

static int
tegra_i2c_iicbus_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr)
{
	struct tegra_i2c_softc *sc;
	int busfreq;

	sc = device_get_softc(dev);
	busfreq = IICBUS_GET_FREQUENCY(sc->iicbus, speed);
	sc = device_get_softc(dev);
	LOCK(sc);
	tegra_i2c_setup_clk(sc, busfreq);
	UNLOCK(sc);
	return (0);
}

static int
tegra_i2c_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	return (BUS_PROBE_DEFAULT);
}

static int
tegra_i2c_attach(device_t dev)
{
	int rv, rid;
	phandle_t node;
	struct tegra_i2c_softc *sc;
	uint64_t freq;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	LOCK_INIT(sc);

	/* Get the memory resource for the register mapping. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot map registers.\n");
		rv = ENXIO;
		goto fail;
	}

	/* Allocate our IRQ resource. */
	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "Cannot allocate interrupt.\n");
		rv = ENXIO;
		goto fail;
	}

	/* FDT resources. */
	rv = clk_get_by_ofw_name(dev, 0, "div-clk", &sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot get i2c clock: %d\n", rv);
		goto fail;
	}
	rv = hwreset_get_by_ofw_name(sc->dev, 0, "i2c", &sc->reset);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get i2c reset\n");
		return (ENXIO);
	}
	rv = OF_getencprop(node, "clock-frequency", &sc->bus_freq,
	    sizeof(sc->bus_freq));
	if (rv != sizeof(sc->bus_freq)) {
		sc->bus_freq = 100000;
		goto fail;
	}

	/* Request maximum frequency for I2C block 136MHz (408MHz / 3). */
	rv = clk_set_freq(sc->clk, 136000000, CLK_SET_ROUND_DOWN);
	if (rv != 0) {
		device_printf(dev, "Cannot set clock frequency\n");
		goto fail;
	}
	rv = clk_get_freq(sc->clk, &freq);
	if (rv != 0) {
		device_printf(dev, "Cannot get clock frequency\n");
		goto fail;
	}
	sc->core_freq = (uint32_t)freq;

	rv = clk_enable(sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot enable clock: %d\n", rv);
		goto fail;
	}

	/* Init hardware. */
	rv = tegra_i2c_hw_init(sc);
	if (rv) {
		device_printf(dev, "tegra_i2c_activate failed\n");
		goto fail;
	}

	/* Setup interrupt. */
	rv = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, tegra_i2c_intr, sc, &sc->irq_h);
	if (rv) {
		device_printf(dev, "Cannot setup interrupt.\n");
		goto fail;
	}

	/* Attach the iicbus. */
	sc->iicbus = device_add_child(dev, "iicbus", -1);
	if (sc->iicbus == NULL) {
		device_printf(dev, "Could not allocate iicbus instance.\n");
		rv = ENXIO;
		goto fail;
	}

	/* Probe and attach the iicbus. */
	return (bus_generic_attach(dev));

fail:
	if (sc->irq_h != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_h);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);
	LOCK_DESTROY(sc);

	return (rv);
}

static int
tegra_i2c_detach(device_t dev)
{
	struct tegra_i2c_softc *sc;
	int rv;

	sc = device_get_softc(dev);
	tegra_i2c_hw_init(sc);
	if (sc->irq_h != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_h);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	LOCK_DESTROY(sc);
	if (sc->iicbus)
	    rv = device_delete_child(dev, sc->iicbus);
	return (bus_generic_detach(dev));
}

static phandle_t
tegra_i2c_get_node(device_t bus, device_t dev)
{

	/* Share controller node with iibus device. */
	return (ofw_bus_get_node(bus));
}

static device_method_t tegra_i2c_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tegra_i2c_probe),
	DEVMETHOD(device_attach,	tegra_i2c_attach),
	DEVMETHOD(device_detach,	tegra_i2c_detach),

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
	DEVMETHOD(ofw_bus_get_node,	tegra_i2c_get_node),

	/* iicbus interface */
	DEVMETHOD(iicbus_callback,	iicbus_null_callback),
	DEVMETHOD(iicbus_reset,		tegra_i2c_iicbus_reset),
	DEVMETHOD(iicbus_transfer,	tegra_i2c_transfer),

	DEVMETHOD_END
};

static devclass_t tegra_i2c_devclass;
static DEFINE_CLASS_0(iichb, tegra_i2c_driver, tegra_i2c_methods,
    sizeof(struct tegra_i2c_softc));
EARLY_DRIVER_MODULE(tegra_iic, simplebus, tegra_i2c_driver, tegra_i2c_devclass,
    NULL, NULL, 73);
