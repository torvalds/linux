/*-
 * Copyright (c) 2017-2018 QCM Technologies.
 * Copyright (c) 2017-2018 Semihalf.
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
 *
 * $FreeBSD$
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>
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
#include <machine/bus.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include "iicbus_if.h"

#include "opal.h"

#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

struct opal_i2c_softc
{
	device_t dev;
	device_t iicbus;
	uint32_t opal_id;
	struct mtx sc_mtx;
};

/* OPAL I2C request */
struct opal_i2c_request {
	uint8_t type;
#define OPAL_I2C_RAW_READ	0
#define OPAL_I2C_RAW_WRITE	1
#define OPAL_I2C_SM_READ	2
#define OPAL_I2C_SM_WRITE	3
	uint8_t flags;
	uint8_t	subaddr_sz;		/* Max 4 */
	uint8_t reserved;
	uint16_t addr;			/* 7 or 10 bit address */
	uint16_t reserved2;
	uint32_t subaddr;		/* Sub-address if any */
	uint32_t size;			/* Data size */
	uint64_t buffer_pa;		/* Buffer real address */
};

static int opal_i2c_attach(device_t);
static int opal_i2c_callback(device_t, int, caddr_t);
static int opal_i2c_probe(device_t);
static int opal_i2c_transfer(device_t, struct iic_msg *, uint32_t);
static int i2c_opal_send_request(uint32_t, struct opal_i2c_request *);
static phandle_t opal_i2c_get_node(device_t bus, device_t dev);

static device_method_t opal_i2c_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		opal_i2c_probe),
	DEVMETHOD(device_attach,	opal_i2c_attach),

	/* iicbus interface */
	DEVMETHOD(iicbus_callback,	opal_i2c_callback),
	DEVMETHOD(iicbus_transfer,	opal_i2c_transfer),
	DEVMETHOD(ofw_bus_get_node,	opal_i2c_get_node),
	DEVMETHOD_END
};

#define	I2C_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	I2C_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	I2C_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->dev), \
	    "i2c", MTX_DEF)

static devclass_t opal_i2c_devclass;

static driver_t opal_i2c_driver = {
	"iichb",
	opal_i2c_methods,
	sizeof(struct opal_i2c_softc),
};

static int
opal_i2c_probe(device_t dev)
{

	if (!(ofw_bus_is_compatible(dev, "ibm,opal-i2c")))
		return (ENXIO);

	device_set_desc(dev, "opal-i2c");

	return (0);
}

static int
opal_i2c_attach(device_t dev)
{
	struct opal_i2c_softc *sc;
	int len;

	sc = device_get_softc(dev);
	sc->dev = dev;

	len = OF_getproplen(ofw_bus_get_node(dev), "ibm,opal-id");
	if (len <= 0)
		return (EINVAL);
	OF_getencprop(ofw_bus_get_node(dev), "ibm,opal-id", &sc->opal_id, len);

	if ((sc->iicbus = device_add_child(dev, "iicbus", -1)) == NULL) {
		device_printf(dev, "could not allocate iicbus instance\n");
		return (EINVAL);
	}

	I2C_LOCK_INIT(sc);

	return (bus_generic_attach(dev));
}

static int
opal_get_async_rc(struct opal_msg msg)
{
	if (msg.msg_type != OPAL_MSG_ASYNC_COMP)
		return OPAL_PARAMETER;
	else
		return htobe64(msg.params[1]);
}

static int
i2c_opal_send_request(uint32_t bus_id, struct opal_i2c_request *req)
{
	struct opal_msg msg;
	uint64_t token;
	int rc;

	token = opal_alloc_async_token();

	memset(&msg, 0, sizeof(msg));

	rc = opal_call(OPAL_I2C_REQUEST, token, bus_id,
	    vtophys(req));
	if (rc != OPAL_ASYNC_COMPLETION)
		goto out;

	rc = opal_wait_completion(&msg, sizeof(msg), token);

	if (rc != OPAL_SUCCESS)
		goto out;

	rc = opal_get_async_rc(msg);

out:
	opal_free_async_token(token);

	return (rc);
}

static int
opal_i2c_transfer(device_t dev, struct iic_msg *msgs, uint32_t nmsgs)
{
	struct opal_i2c_softc *sc;
	int i, err = 0;
	struct opal_i2c_request req;

	sc = device_get_softc(dev);

	memset(&req, 0, sizeof(req));

	I2C_LOCK(sc);
	for (i = 0; i < nmsgs; i++) {
		req.type = (msgs[i].flags & IIC_M_RD) ?
		    OPAL_I2C_RAW_READ : OPAL_I2C_RAW_WRITE;
		req.addr = htobe16(msgs[i].slave >> 1);
		req.size = htobe32(msgs[i].len);
		req.buffer_pa = htobe64(pmap_kextract((uint64_t)msgs[i].buf));

		err = i2c_opal_send_request(sc->opal_id, &req);
	}
	I2C_UNLOCK(sc);

	return (err);
}

static int
opal_i2c_callback(device_t dev, int index, caddr_t data)
{
	int error = 0;

	switch (index) {
	case IIC_REQUEST_BUS:
		break;

	case IIC_RELEASE_BUS:
		break;

	default:
		error = EINVAL;
	}

	return (error);
}

static phandle_t
opal_i2c_get_node(device_t bus, device_t dev)
{

	/* Share controller node with iibus device. */
	return (ofw_bus_get_node(bus));
}

DRIVER_MODULE(opal_i2c, opal_i2cm, opal_i2c_driver, opal_i2c_devclass, NULL,
    NULL);
DRIVER_MODULE(iicbus, opal_i2c, iicbus_driver, iicbus_devclass, NULL, NULL);
MODULE_DEPEND(opal_i2c, iicbus, 1, 1, 1);
