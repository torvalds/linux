/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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
 * AS3722 PMIC driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/sx.h>

#include <machine/bus.h>

#include <dev/extres/regulator/regulator.h>
#include <dev/fdt/fdt_pinctrl.h>
#include <dev/gpio/gpiobusvar.h>
#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <gnu/dts/include/dt-bindings/mfd/as3722.h>

#include "clock_if.h"
#include "regdev_if.h"

#include "as3722.h"

static struct ofw_compat_data compat_data[] = {
	{"ams,as3722",		1},
	{NULL,			0},
};

#define	LOCK(_sc)		sx_xlock(&(_sc)->lock)
#define	UNLOCK(_sc)		sx_xunlock(&(_sc)->lock)
#define	LOCK_INIT(_sc)		sx_init(&(_sc)->lock, "as3722")
#define	LOCK_DESTROY(_sc)	sx_destroy(&(_sc)->lock);
#define	ASSERT_LOCKED(_sc)	sx_assert(&(_sc)->lock, SA_XLOCKED);
#define	ASSERT_UNLOCKED(_sc)	sx_assert(&(_sc)->lock, SA_UNLOCKED);

#define	AS3722_DEVICE_ID	0x0C

/*
 * Raw register access function.
 */
int
as3722_read(struct as3722_softc *sc, uint8_t reg, uint8_t *val)
{
	uint8_t addr;
	int rv;
	struct iic_msg msgs[2] = {
		{0, IIC_M_WR, 1, &addr},
		{0, IIC_M_RD, 1, val},
	};

	msgs[0].slave = sc->bus_addr;
	msgs[1].slave = sc->bus_addr;
	addr = reg;

	rv = iicbus_transfer(sc->dev, msgs, 2);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Error when reading reg 0x%02X, rv: %d\n", reg,  rv);
		return (EIO);
	}

	return (0);
}

int as3722_read_buf(struct as3722_softc *sc, uint8_t reg, uint8_t *buf,
    size_t size)
{
	uint8_t addr;
	int rv;
	struct iic_msg msgs[2] = {
		{0, IIC_M_WR, 1, &addr},
		{0, IIC_M_RD, size, buf},
	};

	msgs[0].slave = sc->bus_addr;
	msgs[1].slave = sc->bus_addr;
	addr = reg;

	rv = iicbus_transfer(sc->dev, msgs, 2);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Error when reading reg 0x%02X, rv: %d\n", reg,  rv);
		return (EIO);
	}

	return (0);
}

int
as3722_write(struct as3722_softc *sc, uint8_t reg, uint8_t val)
{
	uint8_t data[2];
	int rv;

	struct iic_msg msgs[1] = {
		{0, IIC_M_WR, 2, data},
	};

	msgs[0].slave = sc->bus_addr;
	data[0] = reg;
	data[1] = val;

	rv = iicbus_transfer(sc->dev, msgs, 1);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Error when writing reg 0x%02X, rv: %d\n", reg, rv);
		return (EIO);
	}
	return (0);
}

int as3722_write_buf(struct as3722_softc *sc, uint8_t reg, uint8_t *buf,
    size_t size)
{
	uint8_t data[1];
	int rv;
	struct iic_msg msgs[2] = {
		{0, IIC_M_WR, 1, data},
		{0, IIC_M_WR | IIC_M_NOSTART, size, buf},
	};

	msgs[0].slave = sc->bus_addr;
	msgs[1].slave = sc->bus_addr;
	data[0] = reg;

	rv = iicbus_transfer(sc->dev, msgs, 2);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Error when writing reg 0x%02X, rv: %d\n", reg, rv);
		return (EIO);
	}
	return (0);
}

int
as3722_modify(struct as3722_softc *sc, uint8_t reg, uint8_t clear, uint8_t set)
{
	uint8_t val;
	int rv;

	rv = as3722_read(sc, reg, &val);
	if (rv != 0)
		return (rv);

	val &= ~clear;
	val |= set;

	rv = as3722_write(sc, reg, val);
	if (rv != 0)
		return (rv);

	return (0);
}

static int
as3722_get_version(struct as3722_softc *sc)
{
	uint8_t reg;
	int rv;

	/* Verify AS3722 ID and version. */
	rv = RD1(sc, AS3722_ASIC_ID1, &reg);
	if (rv != 0)
		return (ENXIO);

	if (reg != AS3722_DEVICE_ID) {
		device_printf(sc->dev, "Invalid chip ID is 0x%x\n", reg);
		return (ENXIO);
	}

	rv = RD1(sc, AS3722_ASIC_ID2, &sc->chip_rev);
	if (rv != 0)
		return (ENXIO);

	if (bootverbose)
		device_printf(sc->dev, "AS3722 rev: 0x%x\n", sc->chip_rev);
	return (0);
}

static int
as3722_init(struct as3722_softc *sc)
{
	uint32_t reg;
	int rv;

	reg = 0;
	if (sc->int_pullup)
		reg |= AS3722_INT_PULL_UP;
	if (sc->i2c_pullup)
		reg |= AS3722_I2C_PULL_UP;

	rv = RM1(sc, AS3722_IO_VOLTAGE,
	    AS3722_INT_PULL_UP | AS3722_I2C_PULL_UP, reg);
	if (rv != 0)
		return (ENXIO);

	/* mask interrupts */
	rv = WR1(sc, AS3722_INTERRUPT_MASK1, 0);
	if (rv != 0)
		return (ENXIO);
	rv = WR1(sc, AS3722_INTERRUPT_MASK2, 0);
	if (rv != 0)
		return (ENXIO);
	rv = WR1(sc, AS3722_INTERRUPT_MASK3, 0);
	if (rv != 0)
		return (ENXIO);
	rv = WR1(sc, AS3722_INTERRUPT_MASK4, 0);
	if (rv != 0)
		return (ENXIO);
	return (0);
}

static int
as3722_parse_fdt(struct as3722_softc *sc, phandle_t node)
{

	sc->int_pullup =
	    OF_hasprop(node, "ams,enable-internal-int-pullup") ? 1 : 0;
	sc->i2c_pullup =
	    OF_hasprop(node, "ams,enable-internal-i2c-pullup") ? 1 : 0;
	return 0;
}

static void
as3722_intr(void *arg)
{
	struct as3722_softc *sc;

	sc = (struct as3722_softc *)arg;
	/* XXX Finish temperature alarms. */
}

static int
as3722_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "AS3722 PMIC");
	return (BUS_PROBE_DEFAULT);
}

static int
as3722_attach(device_t dev)
{
	struct as3722_softc *sc;
	const char *dname;
	int dunit, rv, rid;
	phandle_t node;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->bus_addr = iicbus_get_addr(dev);
	node = ofw_bus_get_node(sc->dev);
	dname = device_get_name(dev);
	dunit = device_get_unit(dev);
	rv = 0;
	LOCK_INIT(sc);

	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "Cannot allocate interrupt.\n");
		rv = ENXIO;
		goto fail;
	}

	rv = as3722_parse_fdt(sc, node);
	if (rv != 0)
		goto fail;
	rv = as3722_get_version(sc);
	if (rv != 0)
		goto fail;
	rv = as3722_init(sc);
	if (rv != 0)
		goto fail;
	rv = as3722_regulator_attach(sc, node);
	if (rv != 0)
		goto fail;
	rv = as3722_gpio_attach(sc, node);
	if (rv != 0)
		goto fail;
	rv = as3722_rtc_attach(sc, node);
	if (rv != 0)
		goto fail;

	fdt_pinctrl_register(dev, NULL);
	fdt_pinctrl_configure_by_name(dev, "default");

	/* Setup  interrupt. */
	rv = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, as3722_intr, sc, &sc->irq_h);
	if (rv) {
		device_printf(dev, "Cannot setup interrupt.\n");
		goto fail;
	}
	return (bus_generic_attach(dev));

fail:
	if (sc->irq_h != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_h);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	LOCK_DESTROY(sc);
	return (rv);
}

static int
as3722_detach(device_t dev)
{
	struct as3722_softc *sc;

	sc = device_get_softc(dev);
	if (sc->irq_h != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_h);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	LOCK_DESTROY(sc);

	return (bus_generic_detach(dev));
}

static phandle_t
as3722_gpio_get_node(device_t bus, device_t dev)
{

	/* We only have one child, the GPIO bus, which needs our own node. */
	return (ofw_bus_get_node(bus));
}

static device_method_t as3722_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		as3722_probe),
	DEVMETHOD(device_attach,	as3722_attach),
	DEVMETHOD(device_detach,	as3722_detach),

	/* Regdev interface */
	DEVMETHOD(regdev_map,		as3722_regulator_map),

	/* RTC interface */
	DEVMETHOD(clock_gettime,	as3722_rtc_gettime),
	DEVMETHOD(clock_settime,	as3722_rtc_settime),

	/* GPIO protocol interface */
	DEVMETHOD(gpio_get_bus,		as3722_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		as3722_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	as3722_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags,	as3722_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps,	as3722_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags,	as3722_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get,		as3722_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,		as3722_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,	as3722_gpio_pin_toggle),
	DEVMETHOD(gpio_map_gpios,	as3722_gpio_map_gpios),

	/* fdt_pinctrl interface */
	DEVMETHOD(fdt_pinctrl_configure, as3722_pinmux_configure),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	as3722_gpio_get_node),

	DEVMETHOD_END
};

static devclass_t as3722_devclass;
static DEFINE_CLASS_0(gpio, as3722_driver, as3722_methods,
    sizeof(struct as3722_softc));
EARLY_DRIVER_MODULE(as3722, iicbus, as3722_driver, as3722_devclass,
    NULL, NULL, 74);
