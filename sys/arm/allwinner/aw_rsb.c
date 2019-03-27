/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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

/*
 * Allwinner RSB (Reduced Serial Bus) and P2WI (Push-Pull Two Wire Interface)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>

#include "iicbus_if.h"

#define	RSB_CTRL		0x00
#define	 START_TRANS		(1 << 7)
#define	 GLOBAL_INT_ENB		(1 << 1)
#define	 SOFT_RESET		(1 << 0)
#define	RSB_CCR		0x04
#define	RSB_INTE		0x08
#define	RSB_INTS		0x0c
#define	 INT_TRANS_ERR_ID(x)	(((x) >> 8) & 0xf)
#define	 INT_LOAD_BSY		(1 << 2)
#define	 INT_TRANS_ERR		(1 << 1)
#define	 INT_TRANS_OVER		(1 << 0)
#define	 INT_MASK		(INT_LOAD_BSY|INT_TRANS_ERR|INT_TRANS_OVER)
#define	RSB_DADDR0		0x10
#define	RSB_DADDR1		0x14
#define	RSB_DLEN		0x18
#define	 DLEN_READ		(1 << 4)
#define	RSB_DATA0		0x1c
#define	RSB_DATA1		0x20
#define	RSB_CMD			0x2c
#define	 CMD_SRTA		0xe8
#define	 CMD_RD8		0x8b
#define	 CMD_RD16		0x9c
#define	 CMD_RD32		0xa6
#define	 CMD_WR8		0x4e
#define	 CMD_WR16		0x59
#define	 CMD_WR32		0x63
#define	RSB_DAR			0x30
#define	 DAR_RTA		(0xff << 16)
#define	 DAR_RTA_SHIFT		16
#define	 DAR_DA			(0xffff << 0)
#define	 DAR_DA_SHIFT		0

#define	RSB_MAXLEN		8
#define	RSB_RESET_RETRY		100
#define	RSB_I2C_TIMEOUT		hz

#define	RSB_ADDR_PMIC_PRIMARY	0x3a3
#define	RSB_ADDR_PMIC_SECONDARY	0x745
#define	RSB_ADDR_PERIPH_IC	0xe89

#define	A31_P2WI	1
#define	A23_RSB		2

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun6i-a31-p2wi",		A31_P2WI },
	{ "allwinner,sun8i-a23-rsb",		A23_RSB },
	{ NULL,					0 }
};

static struct resource_spec rsb_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

/*
 * Device address to Run-time address mappings.
 *
 * Run-time address (RTA) is an 8-bit value used to address the device during
 * a read or write transaction. The following are valid RTAs:
 *  0x17 0x2d 0x3a 0x4e 0x59 0x63 0x74 0x8b 0x9c 0xa6 0xb1 0xc5 0xd2 0xe8 0xff
 *
 * Allwinner uses RTA 0x2d for the primary PMIC, 0x3a for the secondary PMIC,
 * and 0x4e for the peripheral IC (where applicable).
 */
static const struct {
	uint16_t	addr;
	uint8_t		rta;
} rsb_rtamap[] = {
	{ .addr = RSB_ADDR_PMIC_PRIMARY,	.rta = 0x2d },
	{ .addr = RSB_ADDR_PMIC_SECONDARY,	.rta = 0x3a },
	{ .addr = RSB_ADDR_PERIPH_IC,		.rta = 0x4e },
	{ .addr = 0,				.rta = 0 }
};

struct rsb_softc {
	struct resource	*res;
	struct mtx	mtx;
	clk_t		clk;
	hwreset_t	rst;
	device_t	iicbus;
	int		busy;
	uint32_t	status;
	uint16_t	cur_addr;
	int		type;

	struct iic_msg	*msg;
};

#define	RSB_LOCK(sc)			mtx_lock(&(sc)->mtx)
#define	RSB_UNLOCK(sc)			mtx_unlock(&(sc)->mtx)
#define	RSB_ASSERT_LOCKED(sc)		mtx_assert(&(sc)->mtx, MA_OWNED)
#define	RSB_READ(sc, reg)		bus_read_4((sc)->res, (reg))
#define	RSB_WRITE(sc, reg, val)	bus_write_4((sc)->res, (reg), (val))

static phandle_t
rsb_get_node(device_t bus, device_t dev)
{
	return (ofw_bus_get_node(bus));
}

static int
rsb_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr)
{
	struct rsb_softc *sc;
	int retry;

	sc = device_get_softc(dev);

	RSB_LOCK(sc);

	/* Write soft-reset bit and wait for it to self-clear. */
	RSB_WRITE(sc, RSB_CTRL, SOFT_RESET);
	for (retry = RSB_RESET_RETRY; retry > 0; retry--)
		if ((RSB_READ(sc, RSB_CTRL) & SOFT_RESET) == 0)
			break;

	RSB_UNLOCK(sc);

	if (retry == 0) {
		device_printf(dev, "soft reset timeout\n");
		return (ETIMEDOUT);
	}

	return (IIC_ENOADDR);
}

static uint32_t
rsb_encode(const uint8_t *buf, u_int len, u_int off)
{
	uint32_t val;
	u_int n;

	val = 0;
	for (n = off; n < MIN(len, 4 + off); n++)
		val |= ((uint32_t)buf[n] << ((n - off) * NBBY));

	return val;
}

static void
rsb_decode(const uint32_t val, uint8_t *buf, u_int len, u_int off)
{
	u_int n;

	for (n = off; n < MIN(len, 4 + off); n++)
		buf[n] = (val >> ((n - off) * NBBY)) & 0xff;
}

static int
rsb_start(device_t dev)
{
	struct rsb_softc *sc;
	int error, retry;

	sc = device_get_softc(dev);

	RSB_ASSERT_LOCKED(sc);

	/* Start the transfer */
	RSB_WRITE(sc, RSB_CTRL, GLOBAL_INT_ENB | START_TRANS);

	/* Wait for transfer to complete */
	error = ETIMEDOUT;
	for (retry = RSB_I2C_TIMEOUT; retry > 0; retry--) {
		sc->status |= RSB_READ(sc, RSB_INTS);
		if ((sc->status & INT_TRANS_OVER) != 0) {
			error = 0;
			break;
		}
		DELAY((1000 * hz) / RSB_I2C_TIMEOUT);
	}
	if (error == 0 && (sc->status & INT_TRANS_OVER) == 0) {
		device_printf(dev, "transfer error, status 0x%08x\n",
		    sc->status);
		error = EIO;
	}

	return (error);

}

static int
rsb_set_rta(device_t dev, uint16_t addr)
{
	struct rsb_softc *sc;
	uint8_t rta;
	int i;

	sc = device_get_softc(dev);

	RSB_ASSERT_LOCKED(sc);

	/* Lookup run-time address for given device address */
	for (rta = 0, i = 0; rsb_rtamap[i].rta != 0; i++)
		if (rsb_rtamap[i].addr == addr) {
			rta = rsb_rtamap[i].rta;
			break;
		}
	if (rta == 0) {
		device_printf(dev, "RTA not known for address %#x\n", addr);
		return (ENXIO);
	}

	/* Set run-time address */
	RSB_WRITE(sc, RSB_INTS, RSB_READ(sc, RSB_INTS));
	RSB_WRITE(sc, RSB_DAR, (addr << DAR_DA_SHIFT) | (rta << DAR_RTA_SHIFT));
	RSB_WRITE(sc, RSB_CMD, CMD_SRTA);

	return (rsb_start(dev));
}

static int
rsb_transfer(device_t dev, struct iic_msg *msgs, uint32_t nmsgs)
{
	struct rsb_softc *sc;
	uint32_t daddr[2], data[2], dlen;
	uint16_t device_addr;
	uint8_t cmd;
	int error;

	sc = device_get_softc(dev);

	/*
	 * P2WI and RSB are not really I2C or SMBus controllers, so there are
	 * some restrictions imposed by the driver.
	 *
	 * Transfers must contain exactly two messages. The first is always
	 * a write, containing a single data byte offset. Data will either
	 * be read from or written to the corresponding data byte in the
	 * second message. The slave address in both messages must be the
	 * same.
	 */
	if (nmsgs != 2 || (msgs[0].flags & IIC_M_RD) == IIC_M_RD ||
	    (msgs[0].slave >> 1) != (msgs[1].slave >> 1) ||
	    msgs[0].len != 1 || msgs[1].len > RSB_MAXLEN)
		return (EINVAL);

	/* The RSB controller can read or write 1, 2, or 4 bytes at a time. */
	if (sc->type == A23_RSB) {
		if ((msgs[1].flags & IIC_M_RD) != 0) {
			switch (msgs[1].len) {
			case 1:
				cmd = CMD_RD8;
				break;
			case 2:
				cmd = CMD_RD16;
				break;
			case 4:
				cmd = CMD_RD32;
				break;
			default:
				return (EINVAL);
			}
		} else {
			switch (msgs[1].len) {
			case 1:
				cmd = CMD_WR8;
				break;
			case 2:
				cmd = CMD_WR16;
				break;
			case 4:
				cmd = CMD_WR32;
				break;
			default:
				return (EINVAL);
			}
		}
	}

	RSB_LOCK(sc);
	while (sc->busy)
		mtx_sleep(sc, &sc->mtx, 0, "i2cbuswait", 0);
	sc->busy = 1;
	sc->status = 0;

	/* Select current run-time address if necessary */
	if (sc->type == A23_RSB) {
		device_addr = msgs[0].slave >> 1;
		if (sc->cur_addr != device_addr) {
			error = rsb_set_rta(dev, device_addr);
			if (error != 0)
				goto done;
			sc->cur_addr = device_addr;
			sc->status = 0;
		}
	}

	/* Clear interrupt status */
	RSB_WRITE(sc, RSB_INTS, RSB_READ(sc, RSB_INTS));

	/* Program data access address registers */
	daddr[0] = rsb_encode(msgs[0].buf, msgs[0].len, 0);
	RSB_WRITE(sc, RSB_DADDR0, daddr[0]);

	/* Write data */
	if ((msgs[1].flags & IIC_M_RD) == 0) {
		data[0] = rsb_encode(msgs[1].buf, msgs[1].len, 0);
		RSB_WRITE(sc, RSB_DATA0, data[0]);
	}

	/* Set command type for RSB */
	if (sc->type == A23_RSB)
		RSB_WRITE(sc, RSB_CMD, cmd);

	/* Program data length register and transfer direction */
	dlen = msgs[0].len - 1;
	if ((msgs[1].flags & IIC_M_RD) == IIC_M_RD)
		dlen |= DLEN_READ;
	RSB_WRITE(sc, RSB_DLEN, dlen);

	/* Start transfer */
	error = rsb_start(dev);
	if (error != 0)
		goto done;

	/* Read data */
	if ((msgs[1].flags & IIC_M_RD) == IIC_M_RD) {
		data[0] = RSB_READ(sc, RSB_DATA0);
		rsb_decode(data[0], msgs[1].buf, msgs[1].len, 0);
	}

done:
	sc->msg = NULL;
	sc->busy = 0;
	wakeup(sc);
	RSB_UNLOCK(sc);

	return (error);
}

static int
rsb_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	switch (ofw_bus_search_compatible(dev, compat_data)->ocd_data) {
	case A23_RSB:
		device_set_desc(dev, "Allwinner RSB");
		break;
	case A31_P2WI:
		device_set_desc(dev, "Allwinner P2WI");
		break;
	default:
		return (ENXIO);
	}

	return (BUS_PROBE_DEFAULT);
}

static int
rsb_attach(device_t dev)
{
	struct rsb_softc *sc;
	int error;

	sc = device_get_softc(dev);
	mtx_init(&sc->mtx, device_get_nameunit(dev), "rsb", MTX_DEF);

	sc->type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;

	if (clk_get_by_ofw_index(dev, 0, 0, &sc->clk) == 0) {
		error = clk_enable(sc->clk);
		if (error != 0) {
			device_printf(dev, "cannot enable clock\n");
			goto fail;
		}
	}
	if (hwreset_get_by_ofw_idx(dev, 0, 0, &sc->rst) == 0) {
		error = hwreset_deassert(sc->rst);
		if (error != 0) {
			device_printf(dev, "cannot de-assert reset\n");
			goto fail;
		}
	}

	if (bus_alloc_resources(dev, rsb_spec, &sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		error = ENXIO;
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
	bus_release_resources(dev, rsb_spec, &sc->res);
	if (sc->rst != NULL)
		hwreset_release(sc->rst);
	if (sc->clk != NULL)
		clk_release(sc->clk);
	mtx_destroy(&sc->mtx);
	return (error);
}

static device_method_t rsb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rsb_probe),
	DEVMETHOD(device_attach,	rsb_attach),

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
	DEVMETHOD(ofw_bus_get_node,	rsb_get_node),

	/* iicbus interface */
	DEVMETHOD(iicbus_callback,	iicbus_null_callback),
	DEVMETHOD(iicbus_reset,		rsb_reset),
	DEVMETHOD(iicbus_transfer,	rsb_transfer),

	DEVMETHOD_END
};

static driver_t rsb_driver = {
	"iichb",
	rsb_methods,
	sizeof(struct rsb_softc),
};

static devclass_t rsb_devclass;

EARLY_DRIVER_MODULE(iicbus, rsb, iicbus_driver, iicbus_devclass, 0, 0,
    BUS_PASS_RESOURCE + BUS_PASS_ORDER_MIDDLE);
EARLY_DRIVER_MODULE(rsb, simplebus, rsb_driver, rsb_devclass, 0, 0,
    BUS_PASS_RESOURCE + BUS_PASS_ORDER_MIDDLE);
MODULE_VERSION(rsb, 1);
