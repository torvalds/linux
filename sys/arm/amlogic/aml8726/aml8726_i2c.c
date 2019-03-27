/*-
 * Copyright 2013-2015 John Wehle <john@feith.com>
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
 * Amlogic aml8726 I2C driver.
 *
 * Currently this implementation doesn't take full advantage of the hardware.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
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

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include "iicbb_if.h"


struct aml8726_iic_softc {
	device_t	dev;
	struct resource	*res[1];
	struct mtx	mtx;
	device_t	iicbb;
};

static struct resource_spec aml8726_iic_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ -1, 0 }
};

#define	AML_I2C_LOCK(sc)		mtx_lock(&(sc)->mtx)
#define	AML_I2C_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	AML_I2C_LOCK_INIT(sc)		\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "i2c", MTX_DEF)
#define	AML_I2C_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx);

#define	AML_I2C_CTRL_REG		0
#define	AML_I2C_MANUAL_SDA_I		(1 << 26)
#define	AML_I2C_MANUAL_SCL_I		(1 << 25)
#define	AML_I2C_MANUAL_SDA_O		(1 << 24)
#define	AML_I2C_MANUAL_SCL_O		(1 << 23)
#define	AML_I2C_MANUAL_EN		(1 << 22)

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)

static int
aml8726_iic_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,meson6-i2c"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 I2C");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_iic_attach(device_t dev)
{
	struct aml8726_iic_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;

	if (bus_alloc_resources(dev, aml8726_iic_spec, sc->res)) {
		device_printf(dev, "can not allocate resources for device\n");
		return (ENXIO);
	}

	AML_I2C_LOCK_INIT(sc);

	sc->iicbb = device_add_child(dev, "iicbb", -1);

	if (sc->iicbb == NULL) {
		device_printf(dev, "could not add iicbb\n");
		error = ENXIO;
		goto fail;
	}

	error = device_probe_and_attach(sc->iicbb);

	if (error) {
		device_printf(dev, "could not attach iicbb\n");
		goto fail;
	}

	return (0);

fail:
	AML_I2C_LOCK_DESTROY(sc);
	bus_release_resources(dev, aml8726_iic_spec, sc->res);

	return (error);
}

static int
aml8726_iic_detach(device_t dev)
{
	struct aml8726_iic_softc *sc = device_get_softc(dev);
	device_t child;

	/*
	 * Detach the children before recursively deleting
	 * in case a child has a pointer to a grandchild
	 * which is used by the child's detach routine.
	 *
	 * Remember the child before detaching so we can
	 * delete it (bus_generic_detach indirectly zeroes
	 * sc->child_dev).
	 */
	child = sc->iicbb;
	bus_generic_detach(dev);
	if (child)
		device_delete_child(dev, child);

	AML_I2C_LOCK_DESTROY(sc);

	bus_release_resources(dev, aml8726_iic_spec, sc->res);

	return (0);
}

static void
aml8726_iic_child_detached(device_t dev, device_t child)
{
	struct aml8726_iic_softc *sc = device_get_softc(dev);

	if (child == sc->iicbb)
		sc->iicbb = NULL;
}

static int
aml8726_iic_callback(device_t dev, int index, caddr_t data)
{

	return (0);
}

static int
aml8726_iic_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr)
{
	struct aml8726_iic_softc *sc = device_get_softc(dev);

	AML_I2C_LOCK(sc);

	CSR_WRITE_4(sc, AML_I2C_CTRL_REG,
	    (CSR_READ_4(sc, AML_I2C_CTRL_REG) | AML_I2C_MANUAL_SDA_O |
	    AML_I2C_MANUAL_SCL_O | AML_I2C_MANUAL_EN));

	AML_I2C_UNLOCK(sc);

	/* Wait for 10 usec */
	DELAY(10);

	return (IIC_ENOADDR);
}

static int
aml8726_iic_getscl(device_t dev)
{
	struct aml8726_iic_softc *sc = device_get_softc(dev);

	return (CSR_READ_4(sc, AML_I2C_CTRL_REG) & AML_I2C_MANUAL_SCL_I);
}

static int
aml8726_iic_getsda(device_t dev)
{
	struct aml8726_iic_softc *sc = device_get_softc(dev);

	return (CSR_READ_4(sc, AML_I2C_CTRL_REG) & AML_I2C_MANUAL_SDA_I);
}

static void
aml8726_iic_setscl(device_t dev, int val)
{
	struct aml8726_iic_softc *sc = device_get_softc(dev);

	AML_I2C_LOCK(sc);

	CSR_WRITE_4(sc, AML_I2C_CTRL_REG, ((CSR_READ_4(sc, AML_I2C_CTRL_REG) &
	    ~AML_I2C_MANUAL_SCL_O) | (val ? AML_I2C_MANUAL_SCL_O : 0) |
	    AML_I2C_MANUAL_EN));

	AML_I2C_UNLOCK(sc);
}

static void
aml8726_iic_setsda(device_t dev, int val)
{
	struct aml8726_iic_softc *sc = device_get_softc(dev);

	AML_I2C_LOCK(sc);

	CSR_WRITE_4(sc, AML_I2C_CTRL_REG, ((CSR_READ_4(sc, AML_I2C_CTRL_REG) &
	    ~AML_I2C_MANUAL_SDA_O) | (val ? AML_I2C_MANUAL_SDA_O : 0) |
	    AML_I2C_MANUAL_EN));

	AML_I2C_UNLOCK(sc);
}

static device_method_t aml8726_iic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_iic_probe),
	DEVMETHOD(device_attach,	aml8726_iic_attach),
	DEVMETHOD(device_detach,	aml8726_iic_detach),

	/* bus interface */
	DEVMETHOD(bus_child_detached,	aml8726_iic_child_detached),
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* IICBB interface */
	DEVMETHOD(iicbb_callback,	aml8726_iic_callback),
	DEVMETHOD(iicbb_reset,		aml8726_iic_reset),

	DEVMETHOD(iicbb_getscl,		aml8726_iic_getscl),
	DEVMETHOD(iicbb_getsda,		aml8726_iic_getsda),
	DEVMETHOD(iicbb_setscl,		aml8726_iic_setscl),
	DEVMETHOD(iicbb_setsda,		aml8726_iic_setsda),

	DEVMETHOD_END
};

static driver_t aml8726_iic_driver = {
	"aml8726_iic",
	aml8726_iic_methods,
	sizeof(struct aml8726_iic_softc),
};

static devclass_t aml8726_iic_devclass;

DRIVER_MODULE(aml8726_iic, simplebus, aml8726_iic_driver,
    aml8726_iic_devclass, 0, 0);
DRIVER_MODULE(iicbb, aml8726_iic, iicbb_driver, iicbb_devclass, 0, 0);
MODULE_DEPEND(aml8726_iic, iicbb, IICBB_MINVER, IICBB_PREFVER, IICBB_MAXVER);
MODULE_VERSION(aml8726_iic, 1);
