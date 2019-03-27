/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include <dev/extres/clk/clk.h>

#include "iicbus_if.h"

#include "opt_soc.h"


#define	RK_I2C_CON			0x00
#define	 RK_I2C_CON_EN			(1 << 0)
#define	 RK_I2C_CON_MODE_SHIFT		1
#define	 RK_I2C_CON_MODE_TX		0
#define	 RK_I2C_CON_MODE_RRX		1
#define	 RK_I2C_CON_MODE_RX		2
#define	 RK_I2C_CON_MODE_RTX		3
#define	 RK_I2C_CON_MODE_MASK		0x6
#define	 RK_I2C_CON_START		(1 << 3)
#define	 RK_I2C_CON_STOP		(1 << 4)
#define	 RK_I2C_CON_LASTACK		(1 << 5)
#define	 RK_I2C_CON_NAKSTOP		(1 << 6)

#define	RK_I2C_CLKDIV		0x04
#define	 RK_I2C_CLKDIVL_MASK	0xFFFF
#define	 RK_I2C_CLKDIVL_SHIFT	0
#define	 RK_I2C_CLKDIVH_MASK	0xFFFF0000
#define	 RK_I2C_CLKDIVH_SHIFT	16
#define	 RK_I2C_CLKDIV_MUL	8

#define	RK_I2C_MRXADDR			0x08
#define	 RK_I2C_MRXADDR_SADDR_MASK	0xFFFFFF
#define	 RK_I2C_MRXADDR_VALID(x)	(1 << (24 + x))

#define	RK_I2C_MRXRADDR			0x0C
#define	 RK_I2C_MRXRADDR_SRADDR_MASK	0xFFFFFF
#define	 RK_I2C_MRXRADDR_VALID(x)	(1 << (24 + x))

#define	RK_I2C_MTXCNT		0x10
#define	 RK_I2C_MTXCNT_MASK	0x3F

#define	RK_I2C_MRXCNT		0x14
#define	 RK_I2C_MRXCNT_MASK	0x3F

#define	RK_I2C_IEN		0x18
#define	 RK_I2C_IEN_BTFIEN	(1 << 0)
#define	 RK_I2C_IEN_BRFIEN	(1 << 1)
#define	 RK_I2C_IEN_MBTFIEN	(1 << 2)
#define	 RK_I2C_IEN_MBRFIEN	(1 << 3)
#define	 RK_I2C_IEN_STARTIEN	(1 << 4)
#define	 RK_I2C_IEN_STOPIEN	(1 << 5)
#define	 RK_I2C_IEN_NAKRCVIEN	(1 << 6)
#define	 RK_I2C_IEN_ALL		(RK_I2C_IEN_BTFIEN | \
	RK_I2C_IEN_BRFIEN | RK_I2C_IEN_MBTFIEN | RK_I2C_IEN_MBRFIEN | \
	RK_I2C_IEN_STARTIEN | RK_I2C_IEN_STOPIEN | RK_I2C_IEN_NAKRCVIEN)

#define	RK_I2C_IPD		0x1C
#define	 RK_I2C_IPD_BTFIPD	(1 << 0)
#define	 RK_I2C_IPD_BRFIPD	(1 << 1)
#define	 RK_I2C_IPD_MBTFIPD	(1 << 2)
#define	 RK_I2C_IPD_MBRFIPD	(1 << 3)
#define	 RK_I2C_IPD_STARTIPD	(1 << 4)
#define	 RK_I2C_IPD_STOPIPD	(1 << 5)
#define	 RK_I2C_IPD_NAKRCVIPD	(1 << 6)
#define	 RK_I2C_IPD_ALL		(RK_I2C_IPD_BTFIPD | \
	RK_I2C_IPD_BRFIPD | RK_I2C_IPD_MBTFIPD | RK_I2C_IPD_MBRFIPD | \
	RK_I2C_IPD_STARTIPD | RK_I2C_IPD_STOPIPD | RK_I2C_IPD_NAKRCVIPD)

#define	RK_I2C_FNCT		0x20
#define	 RK_I2C_FNCT_MASK	0x3F

#define	RK_I2C_TXDATA_BASE	0x100

#define	RK_I2C_RXDATA_BASE	0x200

enum rk_i2c_state {
	STATE_IDLE = 0,
	STATE_START,
	STATE_READ,
	STATE_WRITE,
	STATE_STOP
};

struct rk_i2c_softc {
	device_t	dev;
	struct resource	*res[2];
	struct mtx	mtx;
	clk_t		sclk;
	clk_t		pclk;
	int		busy;
	void *		intrhand;
	uint32_t	intr;
	uint32_t	ipd;
	struct iic_msg	*msg;
	size_t		cnt;
	int		transfer_done;
	int		nak_recv;
	uint8_t		mode;
	uint8_t		state;

	device_t	iicbus;
};

static struct ofw_compat_data compat_data[] = {
#ifdef SOC_ROCKCHIP_RK3328
	{"rockchip,rk3328-i2c", 1},
#endif
#ifdef SOC_ROCKCHIP_RK3399
	{"rockchip,rk3399-i2c", 1},
#endif
	{NULL,             0}
};

static struct resource_spec rk_i2c_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static int rk_i2c_probe(device_t dev);
static int rk_i2c_attach(device_t dev);
static int rk_i2c_detach(device_t dev);

#define	RK_I2C_LOCK(sc)			mtx_lock(&(sc)->mtx)
#define	RK_I2C_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	RK_I2C_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->mtx, MA_OWNED)
#define	RK_I2C_READ(sc, reg)		bus_read_4((sc)->res[0], (reg))
#define	RK_I2C_WRITE(sc, reg, val)	bus_write_4((sc)->res[0], (reg), (val))

static uint32_t
rk_i2c_get_clkdiv(struct rk_i2c_softc *sc, uint64_t speed)
{
	uint64_t sclk_freq;
	uint32_t clkdiv;
	int err;

	err = clk_get_freq(sc->sclk, &sclk_freq);
	if (err != 0)
		return (err);

	clkdiv = (sclk_freq / speed / RK_I2C_CLKDIV_MUL / 2) - 1;
	clkdiv &= RK_I2C_CLKDIVL_MASK;

	clkdiv = clkdiv << RK_I2C_CLKDIVH_SHIFT | clkdiv;

	return (clkdiv);
}

static int
rk_i2c_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr)
{
	struct rk_i2c_softc *sc;
	uint32_t clkdiv;
	u_int busfreq;

	sc = device_get_softc(dev);

	busfreq = IICBUS_GET_FREQUENCY(sc->iicbus, speed);

	clkdiv = rk_i2c_get_clkdiv(sc, busfreq);

	RK_I2C_LOCK(sc);

	/* Set the clock divider */
	RK_I2C_WRITE(sc, RK_I2C_CLKDIV, clkdiv);

	/* Disable the module */
	RK_I2C_WRITE(sc, RK_I2C_CON, 0);

	RK_I2C_UNLOCK(sc);

	return (0);
}

static void
rk_i2c_fill_tx(struct rk_i2c_softc *sc)
{
	uint32_t buf32;
	uint8_t buf;
	int i, j, len;

	if (sc->msg == NULL || sc->msg->len == sc->cnt)
		return;

	len = sc->msg->len - sc->cnt;
	if (len > 8)
		len = 8;

	for (i = 0; i < len; i++) {
		buf32 = 0;
		for (j = 0; j < 4 ; j++) {
			if (sc->cnt == sc->msg->len)
				break;

			/* Fill the addr if needed */
			if (sc->cnt == 0) {
				buf = sc->msg->slave;
			}
			else
				buf = sc->msg->buf[sc->cnt - 1];

			buf32 |= buf << (j * 8);

			sc->cnt++;
		}

		RK_I2C_WRITE(sc, RK_I2C_TXDATA_BASE + 4 * i, buf32);

		if (sc->cnt == sc->msg->len)
			break;
	}
}

static void
rk_i2c_drain_rx(struct rk_i2c_softc *sc)
{
	uint32_t buf32 = 0;
	uint8_t buf8;
	int len;
	int i;

	if (sc->msg == NULL) {
		device_printf(sc->dev, "No current iic msg\n");
		return;
	}

	len = sc->msg->len - sc->cnt;
	if (len > 32)
		len = 32;

	for (i = 0; i < len; i++) {
		if (i % 4 == 0)
			buf32 = RK_I2C_READ(sc, RK_I2C_RXDATA_BASE + (i / 4) * 4);

		buf8 = (buf32 >> ((i % 4) * 8)) & 0xFF;

		sc->msg->buf[sc->cnt++] = buf8;
	}
}

static void
rk_i2c_send_start(struct rk_i2c_softc *sc)
{
	uint32_t reg;

	RK_I2C_WRITE(sc, RK_I2C_IEN, RK_I2C_IEN_STARTIEN);

	sc->state = STATE_START;

	reg = RK_I2C_READ(sc, RK_I2C_CON);
	reg |= RK_I2C_CON_START;
	reg |= RK_I2C_CON_EN;
	reg &= ~RK_I2C_CON_MODE_MASK;
	reg |= sc->mode << RK_I2C_CON_MODE_SHIFT;
	RK_I2C_WRITE(sc, RK_I2C_CON, reg);
}

static void
rk_i2c_send_stop(struct rk_i2c_softc *sc)
{
	uint32_t reg;

	RK_I2C_WRITE(sc, RK_I2C_IEN, RK_I2C_IEN_STOPIEN);

	sc->state = STATE_STOP;

	reg = RK_I2C_READ(sc, RK_I2C_CON);
	reg |= RK_I2C_CON_STOP;
	RK_I2C_WRITE(sc, RK_I2C_CON, reg);
}

static void
rk_i2c_intr(void *arg)
{
	struct rk_i2c_softc *sc;
	uint32_t reg;

	sc = (struct rk_i2c_softc *)arg;

	RK_I2C_LOCK(sc);

	sc->ipd = RK_I2C_READ(sc, RK_I2C_IPD);
	RK_I2C_WRITE(sc, RK_I2C_IPD, sc->ipd);

	switch (sc->state) {
	case STATE_START:
		/* Disable start bit */
		reg = RK_I2C_READ(sc, RK_I2C_CON);
		reg &= ~RK_I2C_CON_START;
		RK_I2C_WRITE(sc, RK_I2C_CON, reg);

		if (sc->mode == RK_I2C_CON_MODE_RRX ||
		    sc->mode == RK_I2C_CON_MODE_RX) {
			sc->state = STATE_READ;
			RK_I2C_WRITE(sc, RK_I2C_IEN, RK_I2C_IEN_MBRFIEN |
			    RK_I2C_IEN_NAKRCVIEN);

			reg = RK_I2C_READ(sc, RK_I2C_CON);
			reg |= RK_I2C_CON_LASTACK;
			RK_I2C_WRITE(sc, RK_I2C_CON, reg);

			RK_I2C_WRITE(sc, RK_I2C_MRXCNT, sc->msg->len);
		} else {
			sc->state = STATE_WRITE;
			RK_I2C_WRITE(sc, RK_I2C_IEN, RK_I2C_IEN_MBTFIEN |
			    RK_I2C_IEN_NAKRCVIEN);

			sc->msg->len += 1;
			rk_i2c_fill_tx(sc);
			RK_I2C_WRITE(sc, RK_I2C_MTXCNT, sc->msg->len);
		}
		break;
	case STATE_READ:
		rk_i2c_drain_rx(sc);

		if (sc->cnt == sc->msg->len)
			rk_i2c_send_stop(sc);

		break;
	case STATE_WRITE:
		if (sc->cnt == sc->msg->len)
			rk_i2c_send_stop(sc);

		break;
	case STATE_STOP:
		/* Disable stop bit */
		reg = RK_I2C_READ(sc, RK_I2C_CON);
		reg &= ~RK_I2C_CON_STOP;
		RK_I2C_WRITE(sc, RK_I2C_CON, reg);

		sc->transfer_done = 1;
		sc->state = STATE_IDLE;
		break;
	case STATE_IDLE:
		break;
	}

	wakeup(sc);
	RK_I2C_UNLOCK(sc);
}

static int
rk_i2c_transfer(device_t dev, struct iic_msg *msgs, uint32_t nmsgs)
{
	struct rk_i2c_softc *sc;
	uint32_t reg;
	int i, j, msgskip, err = 0;

	sc = device_get_softc(dev);

	while (sc->busy)
		mtx_sleep(sc, &sc->mtx, 0, "i2cbuswait", 0);

	sc->busy = 1;

	err = clk_enable(sc->pclk);
	if (err != 0) {
		device_printf(dev, "cannot enable pclk clock\n");
		goto out;
	}
	err = clk_enable(sc->sclk);
	if (err != 0) {
		device_printf(dev, "cannot enable i2c clock\n");
		goto out;
	}

	RK_I2C_LOCK(sc);

	/* Clean stale interrupts */
	RK_I2C_WRITE(sc, RK_I2C_IPD, RK_I2C_IPD_ALL);

	for (i = 0; i < nmsgs; i += msgskip) {
		if (nmsgs - i >= 2 && !(msgs[i].flags & IIC_M_RD) &&
		  msgs[i + 1].flags & IIC_M_RD && msgs[i].len <= 4) {
			sc->mode = RK_I2C_CON_MODE_RRX;
			msgskip = 2;
			sc->msg = &msgs[i + 1];

			/* Write slave address */
			reg = msgs[i].slave | RK_I2C_MRXADDR_VALID(0);
			RK_I2C_WRITE(sc, RK_I2C_MRXADDR, reg);
			/* Write slave register address */
			for (j = 0, reg = 0; j < msgs[i].len; j++) {
				reg |= (msgs[i].buf[j] & 0xff) << (j * 8);
				reg |= RK_I2C_MRXADDR_VALID(j);
			}

			RK_I2C_WRITE(sc, RK_I2C_MRXRADDR, reg);
		} else {
			if (msgs[i].flags & IIC_M_RD) {
				sc->mode = RK_I2C_CON_MODE_RX;
				msgs[i].slave |= LSB;
			}
			else {
				sc->mode = RK_I2C_CON_MODE_TX;
				msgs[i].slave &= ~LSB;
			}
			msgskip = 1;
			sc->msg = &msgs[i];
		}

		sc->transfer_done = 0;
		sc->cnt = 0;
		sc->state = STATE_IDLE;
		rk_i2c_send_start(sc);

		while (err == 0 && sc->transfer_done != 1) {
			err = msleep(sc, &sc->mtx, 0, "rk_i2c", 10 * hz);
		}
	}

	/* Disable the module and interrupts */
	RK_I2C_WRITE(sc, RK_I2C_CON, 0);
	RK_I2C_WRITE(sc, RK_I2C_IEN, 0);

	sc->busy = 0;

	RK_I2C_UNLOCK(sc);

	err = clk_disable(sc->pclk);
	if (err != 0) {
		device_printf(dev, "cannot enable pclk clock\n");
		goto out;
	}
	err = clk_disable(sc->sclk);
	if (err != 0) {
		device_printf(dev, "cannot enable i2c clock\n");
		goto out;
	}

out:
	return (err);
}

static int
rk_i2c_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "RockChip I2C");
	return (BUS_PROBE_DEFAULT);
}

static int
rk_i2c_attach(device_t dev)
{
	struct rk_i2c_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	mtx_init(&sc->mtx, device_get_nameunit(dev), "rk_i2c", MTX_DEF);

	if (bus_alloc_resources(dev, rk_i2c_spec, sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		error = ENXIO;
		goto fail;
	}

	if (bus_setup_intr(dev, sc->res[1],
	    INTR_TYPE_MISC | INTR_MPSAFE, NULL, rk_i2c_intr, sc,
	    &sc->intrhand)) {
		bus_release_resources(dev, rk_i2c_spec, sc->res);
		device_printf(dev, "cannot setup interrupt handler\n");
		return (ENXIO);
	}

	clk_set_assigned(dev, ofw_bus_get_node(dev));

	/* Activate the module clocks. */
	error = clk_get_by_ofw_name(dev, 0, "i2c", &sc->sclk);
	if (error != 0) {
		device_printf(dev, "cannot get i2c clock\n");
		goto fail;
	}
	error = clk_get_by_ofw_name(dev, 0, "pclk", &sc->pclk);
	if (error != 0) {
		device_printf(dev, "cannot get pclk clock\n");
		goto fail;
	}

	sc->iicbus = device_add_child(dev, "iicbus", -1);
	if (sc->iicbus == NULL) {
		device_printf(dev, "cannot add iicbus child device\n");
		error = ENXIO;
		goto fail;
	}

	bus_generic_attach(dev);

	return (0);

fail:
	if (rk_i2c_detach(dev) != 0)
		device_printf(dev, "Failed to detach\n");
	return (error);
}

static int
rk_i2c_detach(device_t dev)
{
	struct rk_i2c_softc *sc;
	int error;

	sc = device_get_softc(dev);

	if ((error = bus_generic_detach(dev)) != 0)
		return (error);

	if (sc->iicbus != NULL)
		if ((error = device_delete_child(dev, sc->iicbus)) != 0)
			return (error);

	if (sc->sclk != NULL)
		clk_release(sc->sclk);
	if (sc->pclk != NULL)
		clk_release(sc->pclk);

	if (sc->intrhand != NULL)
		bus_teardown_intr(sc->dev, sc->res[1], sc->intrhand);

	bus_release_resources(dev, rk_i2c_spec, sc->res);

	mtx_destroy(&sc->mtx);

	return (0);
}

static phandle_t
rk_i2c_get_node(device_t bus, device_t dev)
{

	return ofw_bus_get_node(bus);
}

static device_method_t rk_i2c_methods[] = {
	DEVMETHOD(device_probe,		rk_i2c_probe),
	DEVMETHOD(device_attach,	rk_i2c_attach),
	DEVMETHOD(device_detach,	rk_i2c_detach),

	/* OFW methods */
	DEVMETHOD(ofw_bus_get_node,		rk_i2c_get_node),

	DEVMETHOD(iicbus_callback,	iicbus_null_callback),
	DEVMETHOD(iicbus_reset,		rk_i2c_reset),
	DEVMETHOD(iicbus_transfer,	rk_i2c_transfer),

	DEVMETHOD_END
};

static driver_t rk_i2c_driver = {
	"rk_i2c",
	rk_i2c_methods,
	sizeof(struct rk_i2c_softc),
};

static devclass_t rk_i2c_devclass;

EARLY_DRIVER_MODULE(rk_i2c, simplebus, rk_i2c_driver, rk_i2c_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
EARLY_DRIVER_MODULE(ofw_iicbus, rk_i2c, ofw_iicbus_driver, ofw_iicbus_devclass,
    0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
MODULE_DEPEND(rk_i2c, iicbus, 1, 1, 1);
MODULE_DEPEND(rk_i2c, ofw_iicbus, 1, 1, 1);
MODULE_VERSION(rk_i2c, 1);
