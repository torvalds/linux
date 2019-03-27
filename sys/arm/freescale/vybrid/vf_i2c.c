/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 * Vybrid Family Inter-Integrated Circuit (I2C)
 * Chapter 48, Vybrid Reference Manual, Rev. 5, 07/2013
 */

/*
 * This driver is based on the I2C driver for i.MX
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

#include <arm/freescale/vybrid/vf_common.h>

#define	I2C_IBAD	0x0	/* I2C Bus Address Register */
#define	I2C_IBFD	0x1	/* I2C Bus Frequency Divider Register */
#define	I2C_IBCR	0x2	/* I2C Bus Control Register */
#define	 IBCR_MDIS		(1 << 7) /* Module disable. */
#define	 IBCR_IBIE		(1 << 6) /* I-Bus Interrupt Enable. */
#define	 IBCR_MSSL		(1 << 5) /* Master/Slave mode select. */
#define	 IBCR_TXRX		(1 << 4) /* Transmit/Receive mode select. */
#define	 IBCR_NOACK		(1 << 3) /* Data Acknowledge disable. */
#define	 IBCR_RSTA		(1 << 2) /* Repeat Start. */
#define	 IBCR_DMAEN		(1 << 1) /* DMA Enable. */
#define	I2C_IBSR	0x3	/* I2C Bus Status Register */
#define	 IBSR_TCF		(1 << 7) /* Transfer complete. */
#define	 IBSR_IAAS		(1 << 6) /* Addressed as a slave. */
#define	 IBSR_IBB		(1 << 5) /* Bus busy. */
#define	 IBSR_IBAL		(1 << 4) /* Arbitration Lost. */
#define	 IBSR_SRW		(1 << 2) /* Slave Read/Write. */
#define	 IBSR_IBIF		(1 << 1) /* I-Bus Interrupt Flag. */
#define	 IBSR_RXAK		(1 << 0) /* Received Acknowledge. */
#define	I2C_IBDR	0x4	/* I2C Bus Data I/O Register */
#define	I2C_IBIC	0x5	/* I2C Bus Interrupt Config Register */
#define	 IBIC_BIIE		(1 << 7) /* Bus Idle Interrupt Enable bit. */
#define	I2C_IBDBG	0x6	/* I2C Bus Debug Register */

#ifdef DEBUG
#define vf_i2c_dbg(_sc, fmt, args...) \
	device_printf((_sc)->dev, fmt, ##args)
#else
#define vf_i2c_dbg(_sc, fmt, args...)
#endif

static int i2c_repeated_start(device_t, u_char, int);
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

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-i2c"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family Inter-Integrated Circuit (I2C)");
	return (BUS_PROBE_DEFAULT);
}

static int
i2c_attach(device_t dev)
{
	struct i2c_softc *sc;

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

	WRITE1(sc, I2C_IBIC, IBIC_BIIE);

	sc->iicbus = device_add_child(dev, "iicbus", -1);
	if (sc->iicbus == NULL) {
		device_printf(dev, "could not add iicbus child");
		mtx_destroy(&sc->mutex);
		return (ENXIO);
	}

	bus_generic_attach(dev);

	return (0);
}

/* Wait for transfer interrupt flag */
static int
wait_for_iif(struct i2c_softc *sc)
{
	int retry;

	retry = 1000;
	while (retry --) {
		if (READ1(sc, I2C_IBSR) & IBSR_IBIF) {
			WRITE1(sc, I2C_IBSR, IBSR_IBIF);
			return (IIC_NOERR);
		}
		DELAY(10);
	}

	return (IIC_ETIMEOUT);
}

/* Wait for free bus */
static int
wait_for_nibb(struct i2c_softc *sc)
{
	int retry;

	retry = 1000;
	while (retry --) {
		if ((READ1(sc, I2C_IBSR) & IBSR_IBB) == 0)
			return (IIC_NOERR);
		DELAY(10);
	}

	return (IIC_ETIMEOUT);
}

/* Wait for transfer complete+interrupt flag */
static int
wait_for_icf(struct i2c_softc *sc)
{
	int retry;

	retry = 1000;
	while (retry --) {
		if (READ1(sc, I2C_IBSR) & IBSR_TCF) {
			if (READ1(sc, I2C_IBSR) & IBSR_IBIF) {
				WRITE1(sc, I2C_IBSR, IBSR_IBIF);
				return (IIC_NOERR);
			}
		}
		DELAY(10);
	}

	return (IIC_ETIMEOUT);
}

static int
i2c_repeated_start(device_t dev, u_char slave, int timeout)
{
	struct i2c_softc *sc;
	int error;
	int reg;

	sc = device_get_softc(dev);

	vf_i2c_dbg(sc, "i2c repeated start\n");

	mtx_lock(&sc->mutex);

	WRITE1(sc, I2C_IBAD, slave);

	if ((READ1(sc, I2C_IBSR) & IBSR_IBB) == 0) {
		mtx_unlock(&sc->mutex);
		return (IIC_EBUSERR);
	}

	/* Set repeated start condition */
	DELAY(10);

	reg = READ1(sc, I2C_IBCR);
	reg |= (IBCR_RSTA | IBCR_IBIE);
	WRITE1(sc, I2C_IBCR, reg);

	DELAY(10);

	/* Write target address - LSB is R/W bit */
	WRITE1(sc, I2C_IBDR, slave);

	error = wait_for_iif(sc);

	mtx_unlock(&sc->mutex);

	if (error)
		return (error);

	return (IIC_NOERR);
}

static int
i2c_start(device_t dev, u_char slave, int timeout)
{
	struct i2c_softc *sc;
	int error;
	int reg;

	sc = device_get_softc(dev);

	vf_i2c_dbg(sc, "i2c start\n");

	mtx_lock(&sc->mutex);

	WRITE1(sc, I2C_IBAD, slave);

	if (READ1(sc, I2C_IBSR) & IBSR_IBB) {
		mtx_unlock(&sc->mutex);
		vf_i2c_dbg(sc, "cant i2c start: IIC_EBUSBSY\n");
		return (IIC_EBUSERR);
	}

	/* Set start condition */
	reg = (IBCR_MSSL | IBCR_NOACK | IBCR_IBIE);
	WRITE1(sc, I2C_IBCR, reg);

	DELAY(100);

	reg |= (IBCR_TXRX);
	WRITE1(sc, I2C_IBCR, reg);

	/* Write target address - LSB is R/W bit */
	WRITE1(sc, I2C_IBDR, slave);

	error = wait_for_iif(sc);

	mtx_unlock(&sc->mutex);
	if (error) {
		vf_i2c_dbg(sc, "cant i2c start: iif error\n");
		return (error);
	}

	return (IIC_NOERR);
}

static int
i2c_stop(device_t dev)
{
	struct i2c_softc *sc;

	sc = device_get_softc(dev);

	vf_i2c_dbg(sc, "i2c stop\n");

	mtx_lock(&sc->mutex);

	WRITE1(sc, I2C_IBCR, IBCR_NOACK | IBCR_IBIE);

	DELAY(100);

	/* Reset controller if bus still busy after STOP */
	if (wait_for_nibb(sc) == IIC_ETIMEOUT) {
		WRITE1(sc, I2C_IBCR, IBCR_MDIS);
		DELAY(1000);
		WRITE1(sc, I2C_IBCR, IBCR_NOACK);
	}
	mtx_unlock(&sc->mutex);

	return (IIC_NOERR);
}

static int
i2c_reset(device_t dev, u_char speed, u_char addr, u_char *oldadr)
{
	struct i2c_softc *sc;

	sc = device_get_softc(dev);

	vf_i2c_dbg(sc, "i2c reset\n");

	switch (speed) {
	case IIC_FAST:
	case IIC_SLOW:
	case IIC_UNKNOWN:
	case IIC_FASTEST:
	default:
		break;
	}

	mtx_lock(&sc->mutex);
	WRITE1(sc, I2C_IBCR, IBCR_MDIS);

	DELAY(1000);

	WRITE1(sc, I2C_IBFD, 20);
	WRITE1(sc, I2C_IBCR, 0x0); /* Enable i2c */

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

	vf_i2c_dbg(sc, "i2c read\n");

	*read = 0;

	mtx_lock(&sc->mutex);

	if (len) {
		if (len == 1)
			WRITE1(sc, I2C_IBCR, IBCR_IBIE | IBCR_MSSL |	\
			    IBCR_NOACK);
		else
			WRITE1(sc, I2C_IBCR, IBCR_IBIE | IBCR_MSSL);

		/* dummy read */
		READ1(sc, I2C_IBDR);
		DELAY(1000);
	}

	while (*read < len) {
		error = wait_for_icf(sc);
		if (error) {
			mtx_unlock(&sc->mutex);
			return (error);
		}

		if ((*read == len - 2) && last) {
			/* NO ACK on last byte */
			WRITE1(sc, I2C_IBCR, IBCR_IBIE | IBCR_MSSL |	\
			    IBCR_NOACK);
		}

		if ((*read == len - 1) && last) {
			/* Transfer done, remove master bit */
			WRITE1(sc, I2C_IBCR, IBCR_IBIE | IBCR_NOACK);
		}

		*buf++ = READ1(sc, I2C_IBDR);
		(*read)++;
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

	vf_i2c_dbg(sc, "i2c write\n");

	*sent = 0;

	mtx_lock(&sc->mutex);
	while (*sent < len) {

		WRITE1(sc, I2C_IBDR, *buf++);

		error = wait_for_iif(sc);
		if (error) {
			mtx_unlock(&sc->mutex);
			return (error);
		}

		(*sent)++;
	}
	mtx_unlock(&sc->mutex);

	return (IIC_NOERR);
}

static device_method_t i2c_methods[] = {
	DEVMETHOD(device_probe,		i2c_probe),
	DEVMETHOD(device_attach,	i2c_attach),

	DEVMETHOD(iicbus_callback,		iicbus_null_callback),
	DEVMETHOD(iicbus_repeated_start,	i2c_repeated_start),
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
