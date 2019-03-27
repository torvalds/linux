/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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

/*
 * Samsung Exynos 5 Inter-Integrated Circuit (I2C)
 * Chapter 13, Exynos 5 Dual User's Manual Public Rev 1.00
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include "iicbus_if.h"

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/samsung/exynos/exynos5_common.h>

#define	I2CCON		0x00	/* Control register */
#define	 ACKGEN		(1 << 7) /* Acknowledge Enable */
/*
 * Source Clock of I2C-bus Transmit Clock Prescaler
 *
 * 0 = I2CCLK = fPCLK/16
 * 1 = I2CCLK = fPCLK/512
 */
#define	 I2CCLK		(1 << 6)
#define	 IRQ_EN		(1 << 5)	/* Tx/Rx Interrupt Enable/Disable */
#define	 IPEND		(1 << 4)	/* Tx/Rx Interrupt Pending Flag */
#define	 CLKVAL_M	0xf		/* Transmit Clock Prescaler Mask */
#define	 CLKVAL_S	0
#define	I2CSTAT		0x04		/* Control/status register */
#define	 I2CMODE_M	0x3		/* Master/Slave Tx/Rx Mode Select */
#define	 I2CMODE_S	6
#define	 I2CMODE_SR	0x0		/* Slave Receive Mode */
#define	 I2CMODE_ST	0x1		/* Slave Transmit Mode */
#define	 I2CMODE_MR	0x2		/* Master Receive Mode */
#define	 I2CMODE_MT	0x3		/* Master Transmit Mode */
#define	 I2CSTAT_BSY	(1 << 5)	/* Busy Signal Status bit */
#define	 I2C_START_STOP	(1 << 5)	/* Busy Signal Status bit */
#define	 RXTX_EN	(1 << 4)	/* Data Output Enable/Disable */
#define	 ARBST		(1 << 3)	/* Arbitration status flag */
#define	 ADDAS		(1 << 2)	/* Address-as-slave Status Flag */
#define	 ADDZERO	(1 << 1)	/* Address Zero Status Flag */
#define	 ACKRECVD	(1 << 0)	/* Last-received Bit Status Flag */
#define	I2CADD		0x08		/* Address register */
#define	I2CDS		0x0C		/* Transmit/receive data shift */
#define	I2CLC		0x10		/* Multi-master line control */
#define	 FILTER_EN	(1 << 2)	/* Filter Enable bit */
#define	 SDAOUT_DELAY_M	0x3		/* SDA Line Delay Length */
#define	 SDAOUT_DELAY_S	0

#ifdef DEBUG
#define DPRINTF(fmt, args...) \
	printf(fmt, ##args)
#else
#define DPRINTF(fmt, args...)
#endif

static int i2c_start(device_t, u_char, int);
static int i2c_stop(device_t);
static int i2c_reset(device_t, u_char, u_char, u_char *);
static int i2c_read(device_t, char *, int, int *, int, int);
static int i2c_write(device_t, const char *, int, int *, int);

struct i2c_softc {
	struct resource		*res[2];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
	device_t		iicbus;
	struct mtx		mutex;
	void			*ih;
	int			intr;
};

static struct resource_spec i2c_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
i2c_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "exynos,i2c"))
		return (ENXIO);

	device_set_desc(dev, "Samsung Exynos 5 I2C controller");
	return (BUS_PROBE_DEFAULT);
}

static int
clear_ipend(struct i2c_softc *sc)
{
	int reg;

	reg = READ1(sc, I2CCON);
	reg &= ~(IPEND);
	WRITE1(sc, I2CCON, reg);

	return (0);
}

static int
i2c_attach(device_t dev)
{
	struct i2c_softc *sc;
	int reg;

	sc = device_get_softc(dev);
	sc->dev = dev;

	mtx_init(&sc->mutex, device_get_nameunit(dev), "I2C", MTX_DEF);

	if (bus_alloc_resources(dev, i2c_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	sc->iicbus = device_add_child(dev, "iicbus", -1);
	if (sc->iicbus == NULL) {
		device_printf(dev, "could not add iicbus child");
		mtx_destroy(&sc->mutex);
		return (ENXIO);
	}

	WRITE1(sc, I2CSTAT, 0);
	WRITE1(sc, I2CADD, 0x00);

	/* Mode */
	reg = (RXTX_EN);
        reg |= (I2CMODE_MT << I2CMODE_S);
	WRITE1(sc, I2CSTAT, reg);

	bus_generic_attach(dev);

	return (0);
}

static int
wait_for_iif(struct i2c_softc *sc)
{
	int retry;
	int reg;

	retry = 1000;
	while (retry --) {
		reg = READ1(sc, I2CCON);
		if (reg & IPEND) {
			return (IIC_NOERR);
		}
		DELAY(50);
	}

	return (IIC_ETIMEOUT);
}

static int
wait_for_nibb(struct i2c_softc *sc)
{
	int retry;

	retry = 1000;
	while (retry --) {
		if ((READ1(sc, I2CSTAT) & I2CSTAT_BSY) == 0)
			return (IIC_NOERR);
		DELAY(10);
	}

	return (IIC_ETIMEOUT);
}

static int
is_ack(struct i2c_softc *sc)
{
	int stat;

	stat = READ1(sc, I2CSTAT);
	if (!(stat & 1)) {
		/* ACK received */
		return (1);
	}

	return (0);
}

static int
i2c_start(device_t dev, u_char slave, int timeout)
{
	struct i2c_softc *sc;
	int error;
	int reg;

	sc = device_get_softc(dev);

	DPRINTF("i2c start\n");

	mtx_lock(&sc->mutex);

#if 0
	DPRINTF("I2CCON == 0x%08x\n", READ1(sc, I2CCON));
	DPRINTF("I2CSTAT == 0x%08x\n", READ1(sc, I2CSTAT));
#endif

	if (slave & 1) {
		slave &= ~(1);
		slave <<= 1;
		slave |= 1;
	} else {
		slave <<= 1;
	}

	error = wait_for_nibb(sc);
	if (error) {
		mtx_unlock(&sc->mutex);
		DPRINTF("cant i2c start: IIC_EBUSERR\n");
		return (IIC_EBUSERR);
	}

	reg = READ1(sc, I2CCON);
	reg |= (IRQ_EN | ACKGEN);
	WRITE1(sc, I2CCON, reg);

	WRITE1(sc, I2CDS, slave);
	DELAY(50);

	reg = (RXTX_EN);
	reg |= I2C_START_STOP;
	reg |= (I2CMODE_MT << I2CMODE_S);
	WRITE1(sc, I2CSTAT, reg);

	error = wait_for_iif(sc);
	if (error) {
		DPRINTF("cant i2c start: iif error\n");

		mtx_unlock(&sc->mutex);
		return (error);
	}

	if (!is_ack(sc)) {
		DPRINTF("cant i2c start: no ack\n");

		mtx_unlock(&sc->mutex);
		return (IIC_ENOACK);
	}

	mtx_unlock(&sc->mutex);
	return (IIC_NOERR);
}

static int
i2c_stop(device_t dev)
{
	struct i2c_softc *sc;
	int reg;
	int error;

	sc = device_get_softc(dev);

	DPRINTF("i2c stop\n");

	mtx_lock(&sc->mutex);

	reg = READ1(sc, I2CSTAT);
	int mode = (reg >> I2CMODE_S) & I2CMODE_M;

	reg = (RXTX_EN);
	reg |= (mode << I2CMODE_S);
	WRITE1(sc, I2CSTAT, reg);

	clear_ipend(sc);

	error = wait_for_nibb(sc);
	if (error) {
		DPRINTF("cant i2c stop: nibb error\n");
		return (error);
	}

	mtx_unlock(&sc->mutex);
	return (IIC_NOERR);
}

static int
i2c_reset(device_t dev, u_char speed, u_char addr, u_char *oldadr)
{
	struct i2c_softc *sc;

	sc = device_get_softc(dev);

	DPRINTF("i2c reset\n");

	mtx_lock(&sc->mutex);

	/* TODO */

	mtx_unlock(&sc->mutex);

	return (IIC_NOERR);
}

static int
i2c_read(device_t dev, char *buf, int len,
    int *read, int last, int delay)
{
	struct i2c_softc *sc;
	int error;
	int reg;
	uint8_t d;

	sc = device_get_softc(dev);

	DPRINTF("i2c read\n");

	reg = (RXTX_EN);
	reg |= (I2CMODE_MR << I2CMODE_S);
	reg |= I2C_START_STOP;
	WRITE1(sc, I2CSTAT, reg);

	*read = 0;
	mtx_lock(&sc->mutex);

	/* dummy read */
	clear_ipend(sc);
	error = wait_for_iif(sc);
	if (error) {
		DPRINTF("cant i2c read: iif error\n");
		mtx_unlock(&sc->mutex);
		return (error);
	}
	READ1(sc, I2CDS);

	DPRINTF("Read ");
	while (*read < len) {

		/* Do not ack last read */
		if (*read == (len - 1)) {
			reg = READ1(sc, I2CCON);
			reg &= ~(ACKGEN);
			WRITE1(sc, I2CCON, reg);
		}

		clear_ipend(sc);

		error = wait_for_iif(sc);
		if (error) {
			DPRINTF("cant i2c read: iif error\n");
			mtx_unlock(&sc->mutex);
			return (error);
		}

		d = READ1(sc, I2CDS);
		DPRINTF("0x%02x ", d);
		*buf++ = d;
		(*read)++;
	}
	DPRINTF("\n");

	mtx_unlock(&sc->mutex);
	return (IIC_NOERR);
}

static int
i2c_write(device_t dev, const char *buf, int len, int *sent, int timeout)
{
	struct i2c_softc *sc;
	int error;

	sc = device_get_softc(dev);

	DPRINTF("i2c write\n");

	*sent = 0;

	mtx_lock(&sc->mutex);

	DPRINTF("writing ");
	while (*sent < len) {
		uint8_t d = *buf++;
		DPRINTF("0x%02x ", d);

		WRITE1(sc, I2CDS, d);
		DELAY(50);

		clear_ipend(sc);

		error = wait_for_iif(sc);
		if (error) {
			DPRINTF("cant i2c write: iif error\n");
			mtx_unlock(&sc->mutex);
			return (error);
		}

		if (!is_ack(sc)) {
			DPRINTF("cant i2c write: no ack\n");
			mtx_unlock(&sc->mutex);
			return (IIC_ENOACK);
		}

		(*sent)++;
	}
	DPRINTF("\n");

	mtx_unlock(&sc->mutex);
	return (IIC_NOERR);
}

static device_method_t i2c_methods[] = {
	DEVMETHOD(device_probe,		i2c_probe),
	DEVMETHOD(device_attach,	i2c_attach),

	DEVMETHOD(iicbus_callback,		iicbus_null_callback),
	DEVMETHOD(iicbus_start,			i2c_start),
	DEVMETHOD(iicbus_stop,			i2c_stop),
	DEVMETHOD(iicbus_reset,			i2c_reset),
	DEVMETHOD(iicbus_read,			i2c_read),
	DEVMETHOD(iicbus_write,			i2c_write),
	DEVMETHOD(iicbus_transfer,		iicbus_transfer_gen),

	{ 0, 0 }
};

static driver_t i2c_driver = {
	"i2c",
	i2c_methods,
	sizeof(struct i2c_softc),
};

static devclass_t i2c_devclass;

DRIVER_MODULE(i2c, simplebus, i2c_driver, i2c_devclass, 0, 0);
DRIVER_MODULE(iicbus, i2c, iicbus_driver, iicbus_devclass, 0, 0);
