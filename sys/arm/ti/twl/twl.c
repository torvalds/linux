/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
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

/*
 * Texas Instruments TWL4030/TWL5030/TWL60x0/TPS659x0 Power Management and
 * Audio CODEC devices.
 *
 * This code is based on the Linux TWL multifunctional device driver, which is
 * copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * These chips are typically used as support ICs for the OMAP range of embedded
 * ARM processes/SOC from Texas Instruments.  They are typically used to control
 * on board voltages, however some variants have other features like audio
 * codecs, USB OTG transceivers, RTC, PWM, etc.
 *
 * This driver acts as a bus for more specific companion devices.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/mutex.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "arm/ti/twl/twl.h"

/* TWL device IDs */
#define TWL_DEVICE_UNKNOWN          0xffff
#define TWL_DEVICE_4030             0x4030
#define TWL_DEVICE_6025             0x6025
#define TWL_DEVICE_6030             0x6030

/* Each TWL device typically has more than one I2C address */
#define TWL_MAX_SUBADDRS            4

/* The maxium number of bytes that can be written in one call */
#define TWL_MAX_IIC_DATA_SIZE       63

/* The TWL devices typically use 4 I2C address for the different internal
 * register sets, plus one SmartReflex I2C address.
 */
#define TWL_CHIP_ID0                0x48
#define TWL_CHIP_ID1                0x49
#define TWL_CHIP_ID2                0x4A
#define TWL_CHIP_ID3                0x4B

#define TWL_SMARTREFLEX_CHIP_ID     0x12

#define TWL_INVALID_CHIP_ID         0xff

struct twl_softc {
	device_t		sc_dev;
	struct mtx		sc_mtx;
	unsigned int	sc_type;

	uint8_t			sc_subaddr_map[TWL_MAX_SUBADDRS];

	struct intr_config_hook	sc_scan_hook;

	device_t		sc_vreg;
	device_t		sc_clks;
};

/**
 *	Macros for driver mutex locking
 */
#define TWL_LOCK(_sc)             mtx_lock(&(_sc)->sc_mtx)
#define	TWL_UNLOCK(_sc)           mtx_unlock(&(_sc)->sc_mtx)
#define TWL_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->sc_dev), \
	         "twl", MTX_DEF)
#define TWL_LOCK_DESTROY(_sc)     mtx_destroy(&_sc->sc_mtx);
#define TWL_ASSERT_LOCKED(_sc)    mtx_assert(&_sc->sc_mtx, MA_OWNED);
#define TWL_ASSERT_UNLOCKED(_sc)  mtx_assert(&_sc->sc_mtx, MA_NOTOWNED);


/**
 *	twl_is_4030 - returns true if the device is TWL4030
 *	twl_is_6025 - returns true if the device is TWL6025
 *	twl_is_6030 - returns true if the device is TWL6030
 *	@sc: device soft context
 *
 *	Returns a non-zero value if the device matches.
 *
 *	RETURNS:
 *	Returns a non-zero value if the device matches, otherwise zero.
 */
int
twl_is_4030(device_t dev)
{
	struct twl_softc *sc = device_get_softc(dev);
	return (sc->sc_type == TWL_DEVICE_4030);
}

int
twl_is_6025(device_t dev)
{
	struct twl_softc *sc = device_get_softc(dev);
	return (sc->sc_type == TWL_DEVICE_6025);
}

int
twl_is_6030(device_t dev)
{
	struct twl_softc *sc = device_get_softc(dev);
	return (sc->sc_type == TWL_DEVICE_6030);
}


/**
 *	twl_read - read one or more registers from the TWL device
 *	@sc: device soft context
 *	@nsub: the sub-module to read from
 *	@reg: the register offset within the module to read
 *	@buf: buffer to store the bytes in
 *	@cnt: the number of bytes to read
 *
 *	Reads one or more registers and stores the result in the suppled buffer.
 *
 *	RETURNS:
 *	Zero on success or an error code on failure.
 */
int
twl_read(device_t dev, uint8_t nsub, uint8_t reg, uint8_t *buf, uint16_t cnt)
{
	struct twl_softc *sc;
	struct iic_msg msg[2];
	uint8_t addr;
	int rc;

	sc = device_get_softc(dev);

	TWL_LOCK(sc);
	addr = sc->sc_subaddr_map[nsub];
	TWL_UNLOCK(sc);

	if (addr == TWL_INVALID_CHIP_ID)
		return (EIO);


	/* Set the address to read from */
	msg[0].slave = addr;
	msg[0].flags = IIC_M_WR | IIC_M_NOSTOP;
	msg[0].len = 1;
	msg[0].buf = &reg;
	/* Read the data back */
	msg[1].slave = addr;
	msg[1].flags = IIC_M_RD;
	msg[1].len = cnt;
	msg[1].buf = buf;

	rc = iicbus_transfer(dev, msg, 2);
	if (rc != 0) {
		device_printf(dev, "iicbus read failed (adr:0x%02x, reg:0x%02x)\n",
		              addr, reg);
		return (EIO);
	}

	return (0);
}

/**
 *	twl_write - writes one or more registers to the TWL device
 *	@sc: device soft context
 *	@nsub: the sub-module to read from
 *	@reg: the register offset within the module to read
 *	@buf: data to write
 *	@cnt: the number of bytes to write
 *
 *	Writes one or more registers.
 *
 *	RETURNS:
 *	Zero on success or a negative error code on failure.
 */
int
twl_write(device_t dev, uint8_t nsub, uint8_t reg, uint8_t *buf, uint16_t cnt)
{
	struct twl_softc *sc;
	struct iic_msg msg;
	uint8_t addr;
	uint8_t tmp_buf[TWL_MAX_IIC_DATA_SIZE + 1];
	int rc;

	if (cnt > TWL_MAX_IIC_DATA_SIZE)
		return (ENOMEM);

	/* Set the register address as the first byte */
	tmp_buf[0] = reg;
	memcpy(&tmp_buf[1], buf, cnt);

	sc = device_get_softc(dev);

	TWL_LOCK(sc);
	addr = sc->sc_subaddr_map[nsub];
	TWL_UNLOCK(sc);

	if (addr == TWL_INVALID_CHIP_ID)
		return (EIO);


	/* Setup the transfer and execute it */
	msg.slave = addr;
	msg.flags = IIC_M_WR;
	msg.len = cnt + 1;
	msg.buf = tmp_buf;

	rc = iicbus_transfer(dev, &msg, 1);
	if (rc != 0) {
		device_printf(sc->sc_dev, "iicbus write failed (adr:0x%02x, reg:0x%02x)\n",
		              addr, reg);
		return (EIO);
	}

	return (0);
}

/**
 *	twl_test_present - checks if a device with given address is present
 *	@sc: device soft context
 *	@addr: the address of the device to scan for
 *
 *	Sends just the address byte and checks for an ACK. If no ACK then device
 *	is assumed to not be present.
 *
 *	RETURNS:
 *	EIO if device is not present, otherwise 0 is returned.
 */
static int
twl_test_present(struct twl_softc *sc, uint8_t addr)
{
	struct iic_msg msg;
	uint8_t tmp;

	/* Set the address to read from */
	msg.slave = addr;
	msg.flags = IIC_M_RD;
	msg.len = 1;
	msg.buf = &tmp;

	if (iicbus_transfer(sc->sc_dev, &msg, 1) != 0)
		return (EIO);

	return (0);
}

/**
 *	twl_scan - scans the i2c bus for sub modules
 *	@dev: the twl device
 *
 *	TWL devices don't just have one i2c slave address, rather they have up to
 *	5 other addresses, each is for separate modules within the device. This
 *	function scans the bus for 4 possible sub-devices and stores the info
 *	internally.
 *
 */
static void
twl_scan(void *dev)
{
	struct twl_softc *sc;
	unsigned i;
	uint8_t devs[TWL_MAX_SUBADDRS];
	uint8_t base = TWL_CHIP_ID0;

	sc = device_get_softc((device_t)dev);

	memset(devs, TWL_INVALID_CHIP_ID, TWL_MAX_SUBADDRS);

	/* Try each of the addresses (0x48, 0x49, 0x4a & 0x4b) to determine which
	 * sub modules we have.
	 */
	for (i = 0; i < TWL_MAX_SUBADDRS; i++) {
		if (twl_test_present(sc, (base + i)) == 0) {
			devs[i] = (base + i);
			device_printf(sc->sc_dev, "Found (sub)device at 0x%02x\n", (base + i));
		}
	}

	TWL_LOCK(sc);
	memcpy(sc->sc_subaddr_map, devs, TWL_MAX_SUBADDRS);
	TWL_UNLOCK(sc);

	/* Finished with the interrupt hook */
	config_intrhook_disestablish(&sc->sc_scan_hook);
}

/**
 *	twl_probe - 
 *	@dev: the twl device
 *
 *	Scans the FDT for a match for the device, possible compatible device
 *	strings are; "ti,twl6030", "ti,twl6025", "ti,twl4030".  
 *
 *	The FDT compat string also determines the type of device (it is currently
 *	not possible to dynamically determine the device type).
 *
 */
static int
twl_probe(device_t dev)
{
	phandle_t node;
	const char *compat;
	int len, l;
	struct twl_softc *sc;

	if ((compat = ofw_bus_get_compat(dev)) == NULL)
		return (ENXIO);

	if ((node = ofw_bus_get_node(dev)) == 0)
		return (ENXIO);

	/* Get total 'compatible' prop len */
	if ((len = OF_getproplen(node, "compatible")) <= 0)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_type = TWL_DEVICE_UNKNOWN;

	while (len > 0) {
		if (strncasecmp(compat, "ti,twl6030", 10) == 0)
			sc->sc_type = TWL_DEVICE_6030;
		else if (strncasecmp(compat, "ti,twl6025", 10) == 0)
			sc->sc_type = TWL_DEVICE_6025;
		else if (strncasecmp(compat, "ti,twl4030", 10) == 0)
			sc->sc_type = TWL_DEVICE_4030;
		
		if (sc->sc_type != TWL_DEVICE_UNKNOWN)
			break;

		/* Slide to the next sub-string. */
		l = strlen(compat) + 1;
		compat += l;
		len -= l;
	}
	
	switch (sc->sc_type) {
	case TWL_DEVICE_4030:
		device_set_desc(dev, "TI TWL4030/TPS659x0 Companion IC");
		break;
	case TWL_DEVICE_6025:
		device_set_desc(dev, "TI TWL6025 Companion IC");
		break;
	case TWL_DEVICE_6030:
		device_set_desc(dev, "TI TWL6030 Companion IC");
		break;
	case TWL_DEVICE_UNKNOWN:
	default:
		return (ENXIO);
	}
	
	return (0);
}

static int
twl_attach(device_t dev)
{
	struct twl_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	TWL_LOCK_INIT(sc);

	/* We have to wait until interrupts are enabled. I2C read and write
	 * only works if the interrupts are available.
	 */
	sc->sc_scan_hook.ich_func = twl_scan;
	sc->sc_scan_hook.ich_arg = dev;

	if (config_intrhook_establish(&sc->sc_scan_hook) != 0)
		return (ENOMEM);

	/* FIXME: should be in DTS file */
	if ((sc->sc_vreg = device_add_child(dev, "twl_vreg", -1)) == NULL)
		device_printf(dev, "could not allocate twl_vreg instance\n");
	if ((sc->sc_clks = device_add_child(dev, "twl_clks", -1)) == NULL)
		device_printf(dev, "could not allocate twl_clks instance\n");

	return (bus_generic_attach(dev));
}

static int
twl_detach(device_t dev)
{
	struct twl_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_vreg)
		device_delete_child(dev, sc->sc_vreg);
	if (sc->sc_clks)
		device_delete_child(dev, sc->sc_clks);
	

	TWL_LOCK_DESTROY(sc);

	return (0);
}

static device_method_t twl_methods[] = {
	DEVMETHOD(device_probe,		twl_probe),
	DEVMETHOD(device_attach,	twl_attach),
	DEVMETHOD(device_detach,	twl_detach),

	{0, 0},
};

static driver_t twl_driver = {
	"twl",
	twl_methods,
	sizeof(struct twl_softc),
};
static devclass_t twl_devclass;

DRIVER_MODULE(twl, iicbus, twl_driver, twl_devclass, 0, 0);
MODULE_VERSION(twl, 1);
