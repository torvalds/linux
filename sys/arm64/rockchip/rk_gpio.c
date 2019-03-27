/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/extres/clk/clk.h>

#include "opt_soc.h"

#include "gpio_if.h"

#define	RK_GPIO_SWPORTA_DR	0x00	/* Data register */
#define	RK_GPIO_SWPORTA_DDR	0x04	/* Data direction register */

#define	RK_GPIO_INTEN		0x30	/* Interrupt enable register */
#define	RK_GPIO_INTMASK		0x34	/* Interrupt mask register */
#define	RK_GPIO_INTTYPE_LEVEL	0x38	/* Interrupt level register */
#define	RK_GPIO_INT_POLARITY	0x3C	/* Interrupt polarity register */
#define	RK_GPIO_INT_STATUS	0x40	/* Interrupt status register */
#define	RK_GPIO_INT_RAWSTATUS	0x44	/* Raw Interrupt status register */

#define	RK_GPIO_DEBOUNCE	0x48	/* Debounce enable register */

#define	RK_GPIO_PORTA_EOI	0x4C	/* Clear interrupt register */
#define	RK_GPIO_EXT_PORTA	0x50	/* External port register */

#define	RK_GPIO_LS_SYNC		0x60	/* Level sensitive syncronization enable register */

struct rk_gpio_softc {
	device_t		sc_dev;
	device_t		sc_busdev;
	struct mtx		sc_mtx;
	struct resource		*sc_res[2];
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	clk_t			clk;
};

static struct ofw_compat_data compat_data[] = {
	{"rockchip,gpio-bank", 1},
	{NULL,             0}
};

static struct resource_spec rk_gpio_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static int rk_gpio_detach(device_t dev);

#define	RK_GPIO_LOCK(_sc)		mtx_lock_spin(&(_sc)->sc_mtx)
#define	RK_GPIO_UNLOCK(_sc)		mtx_unlock_spin(&(_sc)->sc_mtx)
#define	RK_GPIO_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)

#define	RK_GPIO_WRITE(_sc, _off, _val)		\
    bus_space_write_4(_sc->sc_bst, _sc->sc_bsh, _off, _val)
#define	RK_GPIO_READ(_sc, _off)		\
    bus_space_read_4(_sc->sc_bst, _sc->sc_bsh, _off)

static int
rk_gpio_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "RockChip GPIO Bank controller");
	return (BUS_PROBE_DEFAULT);
}

static int
rk_gpio_attach(device_t dev)
{
	struct rk_gpio_softc *sc;
	phandle_t node;
	int err;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	node = ofw_bus_get_node(sc->sc_dev);
	if (!OF_hasprop(node, "gpio-controller"))
		return (ENXIO);

	mtx_init(&sc->sc_mtx, "rk gpio", "gpio", MTX_SPIN);

	if (bus_alloc_resources(dev, rk_gpio_spec, sc->sc_res)) {
		device_printf(dev, "could not allocate resources\n");
		bus_release_resources(dev, rk_gpio_spec, sc->sc_res);
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}

	sc->sc_bst = rman_get_bustag(sc->sc_res[0]);
	sc->sc_bsh = rman_get_bushandle(sc->sc_res[0]);

	if (clk_get_by_ofw_index(dev, 0, 0, &sc->clk) != 0) {
		device_printf(dev, "Cannot get clock\n");
		rk_gpio_detach(dev);
		return (ENXIO);
	}
	err = clk_enable(sc->clk);
	if (err != 0) {
		device_printf(dev, "Could not enable clock %s\n",
		    clk_get_name(sc->clk));
		rk_gpio_detach(dev);
		return (ENXIO);
	}

	sc->sc_busdev = gpiobus_attach_bus(dev);
	if (sc->sc_busdev == NULL) {
		rk_gpio_detach(dev);
		return (ENXIO);
	}

	return (0);
}

static int
rk_gpio_detach(device_t dev)
{
	struct rk_gpio_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_busdev)
		gpiobus_detach_bus(dev);
	bus_release_resources(dev, rk_gpio_spec, sc->sc_res);
	mtx_destroy(&sc->sc_mtx);
	clk_disable(sc->clk);

	return(0);
}

static device_t
rk_gpio_get_bus(device_t dev)
{
	struct rk_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->sc_busdev);
}

static int
rk_gpio_pin_max(device_t dev, int *maxpin)
{

	/* Each bank have always 32 pins */
	*maxpin = 32;
	return (0);
}

static int
rk_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct rk_gpio_softc *sc;

	sc = device_get_softc(dev);

	if (pin >= 32)
		return (EINVAL);

	RK_GPIO_LOCK(sc);
	snprintf(name, GPIOMAXNAME, "gpio%d", pin);
	RK_GPIO_UNLOCK(sc);

	return (0);
}

static int
rk_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct rk_gpio_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	RK_GPIO_LOCK(sc);
	reg = RK_GPIO_READ(sc, RK_GPIO_SWPORTA_DDR);
	RK_GPIO_UNLOCK(sc);

	if (reg & (1 << pin))
		*flags = GPIO_PIN_OUTPUT;
	else
		*flags = GPIO_PIN_INPUT;

	return (0);
}

static int
rk_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{

	/* Caps are managed by the pinctrl device */
	*caps = 0;
	return (0);
}

static int
rk_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct rk_gpio_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	RK_GPIO_LOCK(sc);

	reg = RK_GPIO_READ(sc, RK_GPIO_SWPORTA_DDR);
	if (flags & GPIO_PIN_INPUT)
		reg &= ~(1 << pin);
	else if (flags & GPIO_PIN_OUTPUT)
		reg |= (1 << pin);

	RK_GPIO_WRITE(sc, RK_GPIO_SWPORTA_DDR, reg);
	RK_GPIO_UNLOCK(sc);

	return (0);
}

static int
rk_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val)
{
	struct rk_gpio_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	RK_GPIO_LOCK(sc);
	reg = RK_GPIO_READ(sc, RK_GPIO_EXT_PORTA);
	RK_GPIO_UNLOCK(sc);

	*val = reg & (1 << pin) ? 1 : 0;

	return (0);
}

static int
rk_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct rk_gpio_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	RK_GPIO_LOCK(sc);
	reg = RK_GPIO_READ(sc, RK_GPIO_SWPORTA_DR);
	if (value)
		reg |= (1 << pin);
	else
		reg &= ~(1 << pin);
	RK_GPIO_WRITE(sc, RK_GPIO_SWPORTA_DR, reg);
	RK_GPIO_UNLOCK(sc);

	return (0);
}

static int
rk_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct rk_gpio_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	RK_GPIO_LOCK(sc);
	reg = RK_GPIO_READ(sc, RK_GPIO_SWPORTA_DR);
	if (reg & (1 << pin))
		reg &= ~(1 << pin);
	else
		reg |= (1 << pin);
	RK_GPIO_WRITE(sc, RK_GPIO_SWPORTA_DR, reg);
	RK_GPIO_UNLOCK(sc);

	return (0);
}

static int
rk_gpio_pin_access_32(device_t dev, uint32_t first_pin, uint32_t clear_pins,
    uint32_t change_pins, uint32_t *orig_pins)
{
	struct rk_gpio_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	RK_GPIO_LOCK(sc);
	reg = RK_GPIO_READ(sc, RK_GPIO_SWPORTA_DR);
	if (orig_pins)
		*orig_pins = reg;

	if ((clear_pins | change_pins) != 0) {
		reg = (reg & ~clear_pins) ^ change_pins;
		RK_GPIO_WRITE(sc, RK_GPIO_SWPORTA_DR, reg);
	}
	RK_GPIO_UNLOCK(sc);

	return (0);
}

static int
rk_gpio_pin_config_32(device_t dev, uint32_t first_pin, uint32_t num_pins,
    uint32_t *pin_flags)
{
	struct rk_gpio_softc *sc;
	uint32_t reg, set, mask, flags;
	int i;

	sc = device_get_softc(dev);

	if (first_pin != 0 || num_pins > 32)
		return (EINVAL);

	set = 0;
	mask = 0;
	for (i = 0; i < num_pins; i++) {
		mask = (mask << 1) | 1;
		flags = pin_flags[i];
		if (flags & GPIO_PIN_INPUT) {
			set &= ~(1 << i);
		} else if (flags & GPIO_PIN_OUTPUT) {
			set |= (1 << i);
		}
	}

	RK_GPIO_LOCK(sc);
	reg = RK_GPIO_READ(sc, RK_GPIO_SWPORTA_DDR);
	reg &= ~mask;
	reg |= set;
	RK_GPIO_WRITE(sc, RK_GPIO_SWPORTA_DDR, reg);
	RK_GPIO_UNLOCK(sc);

	return (0);
}

static int
rk_gpio_map_gpios(device_t bus, phandle_t dev, phandle_t gparent, int gcells,
    pcell_t *gpios, uint32_t *pin, uint32_t *flags)
{

	/* The gpios are mapped as <gpio-phandle pin flags> */
	*pin = gpios[1];
	*flags = gpios[2];
	return (0);
}

static device_method_t rk_gpio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rk_gpio_probe),
	DEVMETHOD(device_attach,	rk_gpio_attach),
	DEVMETHOD(device_detach,	rk_gpio_detach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus,		rk_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		rk_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	rk_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags,	rk_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps,	rk_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags,	rk_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get,		rk_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,		rk_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,	rk_gpio_pin_toggle),
	DEVMETHOD(gpio_pin_access_32,	rk_gpio_pin_access_32),
	DEVMETHOD(gpio_pin_config_32,	rk_gpio_pin_config_32),
	DEVMETHOD(gpio_map_gpios,	rk_gpio_map_gpios),

	DEVMETHOD_END
};

static driver_t rk_gpio_driver = {
	"gpio",
	rk_gpio_methods,
	sizeof(struct rk_gpio_softc),
};

static devclass_t rk_gpio_devclass;

EARLY_DRIVER_MODULE(rk_gpio, simplebus, rk_gpio_driver,
    rk_gpio_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
