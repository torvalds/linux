/*-
 * Copyright (C) 2008 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

/*
 * Driver for the TWSI (aka I2C, aka IIC) bus controller found on Marvell
 * and Allwinner SoCs. Supports master operation only, and works in polling mode.
 *
 * Calls to DELAY() are needed per Application Note AN-179 "TWSI Software
 * Guidelines for Discovery(TM), Horizon (TM) and Feroceon(TM) Devices".
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/_inttypes.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <sys/lock.h>
#include <sys/mutex.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/iicbus/twsi/twsi.h>

#include "iicbus_if.h"

#define	TWSI_CONTROL_ACK	(1 << 2)
#define	TWSI_CONTROL_IFLG	(1 << 3)
#define	TWSI_CONTROL_STOP	(1 << 4)
#define	TWSI_CONTROL_START	(1 << 5)
#define	TWSI_CONTROL_TWSIEN	(1 << 6)
#define	TWSI_CONTROL_INTEN	(1 << 7)

#define	TWSI_STATUS_START		0x08
#define	TWSI_STATUS_RPTD_START		0x10
#define	TWSI_STATUS_ADDR_W_ACK		0x18
#define	TWSI_STATUS_DATA_WR_ACK		0x28
#define	TWSI_STATUS_ADDR_R_ACK		0x40
#define	TWSI_STATUS_DATA_RD_ACK		0x50
#define	TWSI_STATUS_DATA_RD_NOACK	0x58

#define	TWSI_DEBUG
#undef TWSI_DEBUG

#ifdef TWSI_DEBUG
#define	debugf(fmt, args...) do { printf("%s(): ", __func__); printf(fmt,##args); } while (0)
#else
#define	debugf(fmt, args...)
#endif

static struct resource_spec res_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ -1, 0 }
};

static __inline uint32_t
TWSI_READ(struct twsi_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->res[0], off));
}

static __inline void
TWSI_WRITE(struct twsi_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->res[0], off, val);
}

static __inline void
twsi_control_clear(struct twsi_softc *sc, uint32_t mask)
{
	uint32_t val;

	val = TWSI_READ(sc, sc->reg_control);
	val &= ~(TWSI_CONTROL_STOP | TWSI_CONTROL_START);
	val &= ~mask;
	TWSI_WRITE(sc, sc->reg_control, val);
}

static __inline void
twsi_control_set(struct twsi_softc *sc, uint32_t mask)
{
	uint32_t val;

	val = TWSI_READ(sc, sc->reg_control);
	val &= ~(TWSI_CONTROL_STOP | TWSI_CONTROL_START);
	val |= mask;
	TWSI_WRITE(sc, sc->reg_control, val);
}

static __inline void
twsi_clear_iflg(struct twsi_softc *sc)
{

	DELAY(1000);
	twsi_control_clear(sc, TWSI_CONTROL_IFLG);
	DELAY(1000);
}


/*
 * timeout given in us
 * returns
 *   0 on successful mask change
 *   non-zero on timeout
 */
static int
twsi_poll_ctrl(struct twsi_softc *sc, int timeout, uint32_t mask)
{

	timeout /= 10;
	while (!(TWSI_READ(sc, sc->reg_control) & mask)) {
		DELAY(10);
		if (--timeout < 0)
			return (timeout);
	}
	return (0);
}


/*
 * 'timeout' is given in us. Note also that timeout handling is not exact --
 * twsi_locked_start() total wait can be more than 2 x timeout
 * (twsi_poll_ctrl() is called twice). 'mask' can be either TWSI_STATUS_START
 * or TWSI_STATUS_RPTD_START
 */
static int
twsi_locked_start(device_t dev, struct twsi_softc *sc, int32_t mask,
    u_char slave, int timeout)
{
	int read_access, iflg_set = 0;
	uint32_t status;

	mtx_assert(&sc->mutex, MA_OWNED);

	if (mask == TWSI_STATUS_RPTD_START)
		/* read IFLG to know if it should be cleared later; from NBSD */
		iflg_set = TWSI_READ(sc, sc->reg_control) & TWSI_CONTROL_IFLG;

	twsi_control_set(sc, TWSI_CONTROL_START);

	if (mask == TWSI_STATUS_RPTD_START && iflg_set) {
		debugf("IFLG set, clearing\n");
		twsi_clear_iflg(sc);
	}

	/*
	 * Without this delay we timeout checking IFLG if the timeout is 0.
	 * NBSD driver always waits here too.
	 */
	DELAY(1000);

	if (twsi_poll_ctrl(sc, timeout, TWSI_CONTROL_IFLG)) {
		debugf("timeout sending %sSTART condition\n",
		    mask == TWSI_STATUS_START ? "" : "repeated ");
		return (IIC_ETIMEOUT);
	}

	status = TWSI_READ(sc, sc->reg_status);
	if (status != mask) {
		debugf("wrong status (%02x) after sending %sSTART condition\n",
		    status, mask == TWSI_STATUS_START ? "" : "repeated ");
		return (IIC_ESTATUS);
	}

	TWSI_WRITE(sc, sc->reg_data, slave);
	twsi_clear_iflg(sc);
	DELAY(1000);

	if (twsi_poll_ctrl(sc, timeout, TWSI_CONTROL_IFLG)) {
		debugf("timeout sending slave address\n");
		return (IIC_ETIMEOUT);
	}
	
	read_access = (slave & 0x1) ? 1 : 0;
	status = TWSI_READ(sc, sc->reg_status);
	if (status != (read_access ?
	    TWSI_STATUS_ADDR_R_ACK : TWSI_STATUS_ADDR_W_ACK)) {
		debugf("no ACK (status: %02x) after sending slave address\n",
		    status);
		return (IIC_ENOACK);
	}

	return (IIC_NOERR);
}

/*
 * Only slave mode supported, disregard [old]addr
 */
static int
twsi_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr)
{
	struct twsi_softc *sc;
	uint32_t param;

	sc = device_get_softc(dev);

	switch (speed) {
	case IIC_SLOW:
	case IIC_FAST:
		param = sc->baud_rate[speed].param;
		break;
	case IIC_FASTEST:
	case IIC_UNKNOWN:
	default:
		param = sc->baud_rate[IIC_FAST].param;
		break;
	}

	mtx_lock(&sc->mutex);
	TWSI_WRITE(sc, sc->reg_soft_reset, 0x0);
	DELAY(2000);
	TWSI_WRITE(sc, sc->reg_baud_rate, param);
	TWSI_WRITE(sc, sc->reg_control, TWSI_CONTROL_TWSIEN);
	DELAY(1000);
	mtx_unlock(&sc->mutex);

	return (0);
}

static int
twsi_stop(device_t dev)
{
	struct twsi_softc *sc;

	sc = device_get_softc(dev);

	mtx_lock(&sc->mutex);
	twsi_control_clear(sc, TWSI_CONTROL_ACK);
	twsi_control_set(sc, TWSI_CONTROL_STOP);
	twsi_clear_iflg(sc);
	DELAY(1000);
	mtx_unlock(&sc->mutex);

	return (IIC_NOERR);
}

/*
 * timeout is given in us
 */
static int
twsi_repeated_start(device_t dev, u_char slave, int timeout)
{
	struct twsi_softc *sc;
	int rv;

	sc = device_get_softc(dev);

	mtx_lock(&sc->mutex);
	rv = twsi_locked_start(dev, sc, TWSI_STATUS_RPTD_START, slave,
	    timeout);
	mtx_unlock(&sc->mutex);

	if (rv) {
		twsi_stop(dev);
		return (rv);
	} else
		return (IIC_NOERR);
}

/*
 * timeout is given in us
 */
static int
twsi_start(device_t dev, u_char slave, int timeout)
{
	struct twsi_softc *sc;
	int rv;

	sc = device_get_softc(dev);

	mtx_lock(&sc->mutex);
	rv = twsi_locked_start(dev, sc, TWSI_STATUS_START, slave, timeout);
	mtx_unlock(&sc->mutex);

	if (rv) {
		twsi_stop(dev);
		return (rv);
	} else
		return (IIC_NOERR);
}

static int
twsi_read(device_t dev, char *buf, int len, int *read, int last, int delay)
{
	struct twsi_softc *sc;
	uint32_t status;
	int last_byte, rv;

	sc = device_get_softc(dev);

	mtx_lock(&sc->mutex);
	*read = 0;
	while (*read < len) {
		/*
		 * Check if we are reading last byte of the last buffer,
		 * do not send ACK then, per I2C specs
		 */
		last_byte = ((*read == len - 1) && last) ? 1 : 0;
		if (last_byte)
			twsi_control_clear(sc, TWSI_CONTROL_ACK);
		else
			twsi_control_set(sc, TWSI_CONTROL_ACK);

		twsi_clear_iflg(sc);
		DELAY(1000);

		if (twsi_poll_ctrl(sc, delay, TWSI_CONTROL_IFLG)) {
			debugf("timeout reading data\n");
			rv = IIC_ETIMEOUT;
			goto out;
		}

		status = TWSI_READ(sc, sc->reg_status);
		if (status != (last_byte ?
		    TWSI_STATUS_DATA_RD_NOACK : TWSI_STATUS_DATA_RD_ACK)) {
			debugf("wrong status (%02x) while reading\n", status);
			rv = IIC_ESTATUS;
			goto out;
		}

		*buf++ = TWSI_READ(sc, sc->reg_data);
		(*read)++;
	}
	rv = IIC_NOERR;
out:
	mtx_unlock(&sc->mutex);
	return (rv);
}

static int
twsi_write(device_t dev, const char *buf, int len, int *sent, int timeout)
{
	struct twsi_softc *sc;
	uint32_t status;
	int rv;

	sc = device_get_softc(dev);

	mtx_lock(&sc->mutex);
	*sent = 0;
	while (*sent < len) {
		TWSI_WRITE(sc, sc->reg_data, *buf++);

		twsi_clear_iflg(sc);
		DELAY(1000);
		if (twsi_poll_ctrl(sc, timeout, TWSI_CONTROL_IFLG)) {
			debugf("timeout writing data\n");
			rv = IIC_ETIMEOUT;
			goto out;
		}

		status = TWSI_READ(sc, sc->reg_status);
		if (status != TWSI_STATUS_DATA_WR_ACK) {
			debugf("wrong status (%02x) while writing\n", status);
			rv = IIC_ESTATUS;
			goto out;
		}
		(*sent)++;
	}
	rv = IIC_NOERR;
out:
	mtx_unlock(&sc->mutex);
	return (rv);
}

int
twsi_attach(device_t dev)
{
	struct twsi_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	mtx_init(&sc->mutex, device_get_nameunit(dev), "twsi", MTX_DEF);

	if (bus_alloc_resources(dev, res_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		twsi_detach(dev);
		return (ENXIO);
	}

	/* Attach the iicbus. */
	if ((sc->iicbus = device_add_child(dev, "iicbus", -1)) == NULL) {
		device_printf(dev, "could not allocate iicbus instance\n");
		twsi_detach(dev);
		return (ENXIO);
	}
	bus_generic_attach(dev);

	return (0);
}

int
twsi_detach(device_t dev)
{
	struct twsi_softc *sc;
	int rv;

	sc = device_get_softc(dev);
	debugf("");

	if ((rv = bus_generic_detach(dev)) != 0)
		return (rv);

	if (sc->iicbus != NULL)
		if ((rv = device_delete_child(dev, sc->iicbus)) != 0)
			return (rv);

	bus_release_resources(dev, res_spec, sc->res);

	mtx_destroy(&sc->mutex);
	return (0);
}

static device_method_t twsi_methods[] = {
	/* device interface */
	DEVMETHOD(device_detach,	twsi_detach),

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

	/* iicbus interface */
	DEVMETHOD(iicbus_callback, iicbus_null_callback),
	DEVMETHOD(iicbus_repeated_start, twsi_repeated_start),
	DEVMETHOD(iicbus_start,		twsi_start),
	DEVMETHOD(iicbus_stop,		twsi_stop),
	DEVMETHOD(iicbus_write,		twsi_write),
	DEVMETHOD(iicbus_read,		twsi_read),
	DEVMETHOD(iicbus_reset,		twsi_reset),
	DEVMETHOD(iicbus_transfer,	iicbus_transfer_gen),
	{ 0, 0 }
};

DEFINE_CLASS_0(twsi, twsi_driver, twsi_methods,
    sizeof(struct twsi_softc));
