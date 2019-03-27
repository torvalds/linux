/*-
 * Copyright (c) 2015 Michael Gmelin <freebsd@grem.de>
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
 * Driver for intersil I2C ISL29018 Digital Ambient Light Sensor and Proximity
 * Sensor with Interrupt Function, only tested connected over SMBus (ig4iic).
 *
 * Datasheet:
 * http://www.intersil.com/en/products/optoelectronics/ambient-light-and-proximity-sensors/light-to-digital-sensors/ISL29018.html
 * http://www.intersil.com/content/dam/Intersil/documents/isl2/isl29018.pdf
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/lockmgr.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/poll.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/systm.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include <dev/isl/isl.h>

#include "iicbus_if.h"
#include "bus_if.h"
#include "device_if.h"

#define ISL_METHOD_ALS		0x10
#define ISL_METHOD_IR		0x11
#define ISL_METHOD_PROX		0x12
#define ISL_METHOD_RESOLUTION	0x13
#define ISL_METHOD_RANGE	0x14

struct isl_softc {
	device_t	dev;
	struct sx	isl_sx;
};

/* Returns < 0 on problem. */
static int isl_read_sensor(device_t dev, uint8_t cmd_mask);

static int
isl_read_byte(device_t dev, uint8_t reg, uint8_t *val)
{
	uint16_t addr = iicbus_get_addr(dev);
	struct iic_msg msgs[] = {
	     { addr, IIC_M_WR | IIC_M_NOSTOP, 1, &reg },
	     { addr, IIC_M_RD, 1, val },
	};

	return (iicbus_transfer(dev, msgs, nitems(msgs)));
}

static int
isl_write_byte(device_t dev, uint8_t reg, uint8_t val)
{
	uint16_t addr = iicbus_get_addr(dev);
	uint8_t bytes[] = { reg, val };
	struct iic_msg msgs[] = {
	     { addr, IIC_M_WR, nitems(bytes), bytes },
	};

	return (iicbus_transfer(dev, msgs, nitems(msgs)));
}

/*
 * Initialize the device
 */
static int
init_device(device_t dev, int probe)
{
	int error;

	/*
	 * init procedure: send 0x00 to test ref and cmd reg 1
	 */
	error = isl_write_byte(dev, REG_TEST, 0);
	if (error)
		goto done;
	error = isl_write_byte(dev, REG_CMD1, 0);
	if (error)
		goto done;

	pause("islinit", hz/100);

done:
	if (error && !probe)
		device_printf(dev, "Unable to initialize\n");
	return (error);
}

static int isl_probe(device_t);
static int isl_attach(device_t);
static int isl_detach(device_t);

static int isl_sysctl(SYSCTL_HANDLER_ARGS);

static devclass_t isl_devclass;

static device_method_t isl_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		isl_probe),
	DEVMETHOD(device_attach,	isl_attach),
	DEVMETHOD(device_detach,	isl_detach),

	DEVMETHOD_END
};

static driver_t isl_driver = {
	"isl",
	isl_methods,
	sizeof(struct isl_softc),
};

#if 0
static void
isl_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "asl", -1)) {
		if (bootverbose)
			printf("asl: device(s) already created\n");
		return;
	}

	/* Check if we can communicate to our slave. */
	if (init_device(dev, 0x88, 1) == 0)
		BUS_ADD_CHILD(parent, ISA_ORDER_SPECULATIVE, "isl", -1);
}
#endif

static int
isl_probe(device_t dev)
{
	uint32_t addr = iicbus_get_addr(dev);

	if (addr != 0x88)
		return (ENXIO);
	if (init_device(dev, 1) != 0)
		return (ENXIO);
	device_set_desc(dev, "ISL Digital Ambient Light Sensor");
	return (BUS_PROBE_VENDOR);
}

static int
isl_attach(device_t dev)
{
	struct isl_softc *sc;
	struct sysctl_ctx_list *sysctl_ctx;
	struct sysctl_oid *sysctl_tree;
	int use_als;
	int use_ir;
	int use_prox;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (init_device(dev, 0) != 0)
		return (ENXIO);

	sx_init(&sc->isl_sx, "ISL read lock");

	sysctl_ctx = device_get_sysctl_ctx(dev);
	sysctl_tree = device_get_sysctl_tree(dev);

	use_als = isl_read_sensor(dev, CMD1_MASK_ALS_ONCE) >= 0;
	use_ir = isl_read_sensor(dev, CMD1_MASK_IR_ONCE) >= 0;
	use_prox = isl_read_sensor(dev, CMD1_MASK_PROX_ONCE) >= 0;

	if (use_als) {
		SYSCTL_ADD_PROC(sysctl_ctx,
			SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
			    "als", CTLTYPE_INT | CTLFLAG_RD,
			    sc, ISL_METHOD_ALS, isl_sysctl, "I",
			    "Current ALS sensor read-out");
	}

	if (use_ir) {
		SYSCTL_ADD_PROC(sysctl_ctx,
			SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
			    "ir", CTLTYPE_INT | CTLFLAG_RD,
			    sc, ISL_METHOD_IR, isl_sysctl, "I",
			    "Current IR sensor read-out");
	}

	if (use_prox) {
		SYSCTL_ADD_PROC(sysctl_ctx,
			SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
			    "prox", CTLTYPE_INT | CTLFLAG_RD,
			    sc, ISL_METHOD_PROX, isl_sysctl, "I",
			    "Current proximity sensor read-out");
	}

	SYSCTL_ADD_PROC(sysctl_ctx,
		SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
		    "resolution", CTLTYPE_INT | CTLFLAG_RD,
		    sc, ISL_METHOD_RESOLUTION, isl_sysctl, "I",
		    "Current proximity sensor resolution");

	SYSCTL_ADD_PROC(sysctl_ctx,
	SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "range", CTLTYPE_INT | CTLFLAG_RD,
	    sc, ISL_METHOD_RANGE, isl_sysctl, "I",
	    "Current proximity sensor range");

	return (0);
}

static int
isl_detach(device_t dev)
{
	struct isl_softc *sc;

	sc = device_get_softc(dev);
	sx_destroy(&sc->isl_sx);

	return (0);
}

static int
isl_sysctl(SYSCTL_HANDLER_ARGS)
{
	static int resolutions[] = { 16, 12, 8, 4};
	static int ranges[] = { 1000, 4000, 16000, 64000};

	struct isl_softc *sc;
	uint8_t rbyte;
	int arg;
	int resolution;
	int range;

	sc = (struct isl_softc *)oidp->oid_arg1;
	arg = -1;

	sx_xlock(&sc->isl_sx);
	if (isl_read_byte(sc->dev, REG_CMD2, &rbyte) != 0) {
		sx_xunlock(&sc->isl_sx);
		return (-1);
	}
	resolution = resolutions[(rbyte & CMD2_MASK_RESOLUTION)
			    >> CMD2_SHIFT_RESOLUTION];
	range = ranges[(rbyte & CMD2_MASK_RANGE) >> CMD2_SHIFT_RANGE];

	switch (oidp->oid_arg2) {
	case ISL_METHOD_ALS:
		arg = (isl_read_sensor(sc->dev,
		    CMD1_MASK_ALS_ONCE) * range) >> resolution;
		break;
	case ISL_METHOD_IR:
		arg = isl_read_sensor(sc->dev, CMD1_MASK_IR_ONCE);
		break;
	case ISL_METHOD_PROX:
		arg = isl_read_sensor(sc->dev, CMD1_MASK_PROX_ONCE);
		break;
	case ISL_METHOD_RESOLUTION:
		arg = (1 << resolution);
		break;
	case ISL_METHOD_RANGE:
		arg = range;
		break;
	}
	sx_xunlock(&sc->isl_sx);

	SYSCTL_OUT(req, &arg, sizeof(arg));
	return (0);
}

static int
isl_read_sensor(device_t dev, uint8_t cmd_mask)
{
	uint8_t rbyte;
	uint8_t cmd;
	int ret;

	if (isl_read_byte(dev, REG_CMD1, &rbyte) != 0) {
		device_printf(dev,
		    "Couldn't read first byte before issuing command %d\n",
		    cmd_mask);
		return (-1);
	}

	cmd = (rbyte & 0x1f) | cmd_mask;
	if (isl_write_byte(dev, REG_CMD1, cmd) != 0) {
		device_printf(dev, "Couldn't write command %d\n", cmd_mask);
		return (-1);
	}

	pause("islconv", hz/10);

	if (isl_read_byte(dev, REG_DATA1, &rbyte) != 0) {
		device_printf(dev,
		    "Couldn't read first byte after command %d\n", cmd_mask);
		return (-1);
	}

	ret = rbyte;
	if (isl_read_byte(dev, REG_DATA2, &rbyte) != 0) {
		device_printf(dev, "Couldn't read second byte after command %d\n", cmd_mask);
		return (-1);
	}
	ret += rbyte << 8;

	return (ret);
}

DRIVER_MODULE(isl, iicbus, isl_driver, isl_devclass, NULL, NULL);
MODULE_DEPEND(isl, iicbus, IICBUS_MINVER, IICBUS_PREFVER, IICBUS_MAXVER);
MODULE_VERSION(isl, 1);
