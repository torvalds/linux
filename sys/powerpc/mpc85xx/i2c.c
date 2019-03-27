/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008-2009 Semihalf, Michal Hajduk
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <sys/lock.h>
#include <sys/mutex.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include "iicbus_if.h"

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#define I2C_ADDR_REG		0x00 /* I2C slave address register */
#define I2C_FDR_REG		0x04 /* I2C frequency divider register */
#define I2C_CONTROL_REG		0x08 /* I2C control register */
#define I2C_STATUS_REG		0x0C /* I2C status register */
#define I2C_DATA_REG		0x10 /* I2C data register */
#define I2C_DFSRR_REG		0x14 /* I2C Digital Filter Sampling rate */
#define I2C_ENABLE		0x80 /* Module enable - interrupt disable */
#define I2CSR_RXAK		0x01 /* Received acknowledge */
#define I2CSR_MCF		(1<<7) /* Data transfer */
#define I2CSR_MASS		(1<<6) /* Addressed as a slave */
#define I2CSR_MBB		(1<<5) /* Bus busy */
#define I2CSR_MAL		(1<<4) /* Arbitration lost */
#define I2CSR_SRW		(1<<2) /* Slave read/write */
#define I2CSR_MIF		(1<<1) /* Module interrupt */
#define I2CCR_MEN		(1<<7) /* Module enable */
#define I2CCR_MSTA		(1<<5) /* Master/slave mode */
#define I2CCR_MTX		(1<<4) /* Transmit/receive mode */
#define I2CCR_TXAK		(1<<3) /* Transfer acknowledge */
#define I2CCR_RSTA		(1<<2) /* Repeated START */

#define I2C_BAUD_RATE_FAST	0x31
#define I2C_BAUD_RATE_DEF	0x3F
#define I2C_DFSSR_DIV		0x10

#ifdef  DEBUG
#define debugf(fmt, args...) do { printf("%s(): ", __func__); printf(fmt,##args); } while (0)
#else
#define debugf(fmt, args...)
#endif

struct i2c_softc {
	device_t		dev;
	device_t		iicbus;
	struct resource		*res;
	struct mtx		mutex;
	int			rid;
	bus_space_handle_t	bsh;
	bus_space_tag_t		bst;
};

static int i2c_probe(device_t);
static int i2c_attach(device_t);

static int i2c_repeated_start(device_t dev, u_char slave, int timeout);
static int i2c_start(device_t dev, u_char slave, int timeout);
static int i2c_stop(device_t dev);
static int i2c_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr);
static int i2c_read(device_t dev, char *buf, int len, int *read, int last, int delay);
static int i2c_write(device_t dev, const char *buf, int len, int *sent, int timeout);
static phandle_t i2c_get_node(device_t bus, device_t dev);

static device_method_t i2c_methods[] = {
	DEVMETHOD(device_probe,			i2c_probe),
	DEVMETHOD(device_attach,		i2c_attach),

	DEVMETHOD(iicbus_callback,		iicbus_null_callback),
	DEVMETHOD(iicbus_repeated_start,	i2c_repeated_start),
	DEVMETHOD(iicbus_start,			i2c_start),
	DEVMETHOD(iicbus_stop,			i2c_stop),
	DEVMETHOD(iicbus_reset,			i2c_reset),
	DEVMETHOD(iicbus_read,			i2c_read),
	DEVMETHOD(iicbus_write,			i2c_write),
	DEVMETHOD(iicbus_transfer,		iicbus_transfer_gen),
	DEVMETHOD(ofw_bus_get_node,		i2c_get_node),

	{ 0, 0 }
};

static driver_t i2c_driver = {
	"iichb",
	i2c_methods,
	sizeof(struct i2c_softc),
};
static devclass_t  i2c_devclass;

DRIVER_MODULE(i2c, simplebus, i2c_driver, i2c_devclass, 0, 0);
DRIVER_MODULE(iicbus, i2c, iicbus_driver, iicbus_devclass, 0, 0);

static __inline void
i2c_write_reg(struct i2c_softc *sc, bus_size_t off, uint8_t val)
{

	bus_space_write_1(sc->bst, sc->bsh, off, val);
}

static __inline uint8_t
i2c_read_reg(struct i2c_softc *sc, bus_size_t off)
{

	return (bus_space_read_1(sc->bst, sc->bsh, off));
}

static __inline void
i2c_flag_set(struct i2c_softc *sc, bus_size_t off, uint8_t mask)
{
	uint8_t status;

	status = i2c_read_reg(sc, off);
	status |= mask;
	i2c_write_reg(sc, off, status);
}

static int
i2c_do_wait(device_t dev, struct i2c_softc *sc, int write, int start)
{
	int err;
	uint8_t status;

	status = i2c_read_reg(sc, I2C_STATUS_REG);
	if (status & I2CSR_MIF) {
		if (write && start && (status & I2CSR_RXAK)) {
			debugf("no ack %s", start ?
			    "after sending slave address" : "");
			err = IIC_ENOACK;
			goto error;
		}
		if (status & I2CSR_MAL) {
			debugf("arbitration lost");
			err = IIC_EBUSERR;
			goto error;
		}
		if (!write && !(status & I2CSR_MCF)) {
			debugf("transfer unfinished");
			err = IIC_EBUSERR;
			goto error;
		}
	}

	return (IIC_NOERR);

error:
	i2c_write_reg(sc, I2C_STATUS_REG, 0x0);
	i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN | I2CCR_TXAK);
	return (err);
}

static int
i2c_probe(device_t dev)
{
	struct i2c_softc *sc;

	if (!ofw_bus_is_compatible(dev, "fsl-i2c"))
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->rid = 0;

	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid,
	    RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->res);
	sc->bsh = rman_get_bushandle(sc->res);

	/* Enable I2C */
	i2c_write_reg(sc, I2C_CONTROL_REG, I2C_ENABLE);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->res);
	device_set_desc(dev, "I2C bus controller");

	return (BUS_PROBE_DEFAULT);
}

static int
i2c_attach(device_t dev)
{
	struct i2c_softc *sc;
	sc = device_get_softc(dev);

	sc->dev = dev;
	sc->rid = 0;

	mtx_init(&sc->mutex, device_get_nameunit(dev), "I2C", MTX_DEF);

	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid,
	    RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "could not allocate resources");
		mtx_destroy(&sc->mutex);
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->res);
	sc->bsh = rman_get_bushandle(sc->res);

	sc->iicbus = device_add_child(dev, "iicbus", -1);
	if (sc->iicbus == NULL) {
		device_printf(dev, "could not add iicbus child");
		mtx_destroy(&sc->mutex);
		return (ENXIO);
	}

	bus_generic_attach(dev);
	return (IIC_NOERR);
}
static int
i2c_repeated_start(device_t dev, u_char slave, int timeout)
{
	struct i2c_softc *sc;
	int error;
	
	sc = device_get_softc(dev);

	mtx_lock(&sc->mutex);
	/* Set repeated start condition */
	i2c_flag_set(sc, I2C_CONTROL_REG ,I2CCR_RSTA);
	/* Write target address - LSB is R/W bit */
	i2c_write_reg(sc, I2C_DATA_REG, slave);
	DELAY(1250);

	error = i2c_do_wait(dev, sc, 1, 1);
	mtx_unlock(&sc->mutex);

	if (error)
		return (error);

	return (IIC_NOERR);
}

static int
i2c_start(device_t dev, u_char slave, int timeout)
{
	struct i2c_softc *sc;
	uint8_t status;
	int error;

	sc = device_get_softc(dev);
	DELAY(1000);

	mtx_lock(&sc->mutex);
	status = i2c_read_reg(sc, I2C_STATUS_REG);
	/* Check if bus is idle or busy */
	if (status & I2CSR_MBB) {
		debugf("bus busy");
		mtx_unlock(&sc->mutex);
		i2c_stop(dev);
		return (IIC_EBUSERR);
	}

	/* Set start condition */
	i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN | I2CCR_MSTA | I2CCR_MTX);
	/* Write target address - LSB is R/W bit */
	i2c_write_reg(sc, I2C_DATA_REG, slave);
	DELAY(1250);

	error = i2c_do_wait(dev, sc, 1, 1);

	mtx_unlock(&sc->mutex);
	if (error)
		return (error);

	return (IIC_NOERR);
}

static int
i2c_stop(device_t dev)
{
	struct i2c_softc *sc;

	sc = device_get_softc(dev);
	mtx_lock(&sc->mutex);
	i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN | I2CCR_TXAK);
	DELAY(1000);
	mtx_unlock(&sc->mutex);

	return (IIC_NOERR);
}

static int
i2c_reset(device_t dev, u_char speed, u_char addr, u_char *oldadr)
{
	struct i2c_softc *sc;
	uint8_t baud_rate;

	sc = device_get_softc(dev);

	switch (speed) {
	case IIC_FAST:
		baud_rate = I2C_BAUD_RATE_FAST;
		break;
	case IIC_SLOW:
	case IIC_UNKNOWN:
	case IIC_FASTEST:
	default:
		baud_rate = I2C_BAUD_RATE_DEF;
		break;
	}

	mtx_lock(&sc->mutex);
	i2c_write_reg(sc, I2C_CONTROL_REG, 0x0);
	i2c_write_reg(sc, I2C_STATUS_REG, 0x0);
	DELAY(1000);
	i2c_write_reg(sc, I2C_FDR_REG, baud_rate);
	i2c_write_reg(sc, I2C_DFSRR_REG, I2C_DFSSR_DIV);
	i2c_write_reg(sc, I2C_CONTROL_REG, I2C_ENABLE);
	DELAY(1000);
	mtx_unlock(&sc->mutex);

	return (IIC_NOERR);
}

static int
i2c_read(device_t dev, char *buf, int len, int *read, int last, int delay)
{
	struct i2c_softc *sc;
	int error;

	sc = device_get_softc(dev);
	*read = 0;

	mtx_lock(&sc->mutex);
	if (len) {
		if (len == 1)
			i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN |
			    I2CCR_MSTA | I2CCR_TXAK);

		else
			i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN |
			    I2CCR_MSTA);

		/* dummy read */
		i2c_read_reg(sc, I2C_DATA_REG);
		DELAY(1000);
	}

	while (*read < len) {
		DELAY(1000);
		error = i2c_do_wait(dev, sc, 0, 0);
		if (error) {
			mtx_unlock(&sc->mutex);
			return (error);
		}
		if ((*read == len - 2) && last) {
			i2c_write_reg(sc, I2C_CONTROL_REG, I2CCR_MEN |
			    I2CCR_MSTA | I2CCR_TXAK);
		}

		if ((*read == len - 1) && last) {
			i2c_write_reg(sc, I2C_CONTROL_REG,  I2CCR_MEN |
			    I2CCR_TXAK);
		}

		*buf++ = i2c_read_reg(sc, I2C_DATA_REG);
		(*read)++;
		DELAY(1250);
	}
	mtx_unlock(&sc->mutex);

	return (IIC_NOERR);
}

static int
i2c_write(device_t dev, const char *buf, int len, int *sent, int timeout)
{
	struct i2c_softc *sc;
	int error;

	sc = device_get_softc(dev);
	*sent = 0;

	mtx_lock(&sc->mutex);
	while (*sent < len) {
		i2c_write_reg(sc, I2C_DATA_REG, *buf++);
		DELAY(1250);

		error = i2c_do_wait(dev, sc, 1, 0);
		if (error) {
			mtx_unlock(&sc->mutex);
			return (error);
		}

		(*sent)++;
	}
	mtx_unlock(&sc->mutex);

	return (IIC_NOERR);
}

static phandle_t
i2c_get_node(device_t bus, device_t dev)
{

	/* Share controller node with iibus device. */
	return (ofw_bus_get_node(bus));
}
