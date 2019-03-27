/*-
 * Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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

#include "opt_platform.h"
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/mutex.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif
#include <dev/gpio/gpiobusvar.h>
#include <dev/extres/regulator/regulator_fixed.h>

#include "regdev_if.h"

MALLOC_DEFINE(M_FIXEDREGULATOR, "fixedregulator", "Fixed regulator");

/* GPIO list for shared pins. */
typedef TAILQ_HEAD(gpio_list, gpio_entry) gpio_list_t;
struct gpio_entry {
	TAILQ_ENTRY(gpio_entry)	link;
	struct gpiobus_pin	gpio_pin;
	int 			use_cnt;
	int 			enable_cnt;
	bool			always_on;
};
static gpio_list_t gpio_list = TAILQ_HEAD_INITIALIZER(gpio_list);
static struct mtx gpio_list_mtx;
MTX_SYSINIT(gpio_list_lock, &gpio_list_mtx, "Regulator GPIO lock", MTX_DEF);

struct regnode_fixed_sc {
	struct regnode_std_param *param;
	bool			gpio_open_drain;
	struct gpio_entry	*gpio_entry;
};

static int regnode_fixed_init(struct regnode *regnode);
static int regnode_fixed_enable(struct regnode *regnode, bool enable,
    int *udelay);
static int regnode_fixed_status(struct regnode *regnode, int *status);
static int regnode_fixed_stop(struct regnode *regnode, int *udelay);

static regnode_method_t regnode_fixed_methods[] = {
	/* Regulator interface */
	REGNODEMETHOD(regnode_init,		regnode_fixed_init),
	REGNODEMETHOD(regnode_enable,		regnode_fixed_enable),
	REGNODEMETHOD(regnode_status,		regnode_fixed_status),
	REGNODEMETHOD(regnode_stop,		regnode_fixed_stop),
	REGNODEMETHOD_END
};
DEFINE_CLASS_1(regnode_fixed, regnode_fixed_class, regnode_fixed_methods,
   sizeof(struct regnode_fixed_sc), regnode_class);

/*
 * GPIO list functions.
 * Two or more regulators can share single GPIO pins, so we must track all
 * GPIOs in gpio_list.
 * The GPIO pin is registerd and reseved for first consumer, all others share
 * gpio_entry with it.
 */
static struct gpio_entry *
regnode_get_gpio_entry(struct gpiobus_pin *gpio_pin)
{
	struct gpio_entry *entry, *tmp;
	device_t busdev;
	int rv;

	busdev = GPIO_GET_BUS(gpio_pin->dev);
	if (busdev == NULL)
		return (NULL);
	entry = malloc(sizeof(struct gpio_entry), M_FIXEDREGULATOR,
	    M_WAITOK | M_ZERO);

	mtx_lock(&gpio_list_mtx);

	TAILQ_FOREACH(tmp, &gpio_list, link) {
		if (tmp->gpio_pin.dev == gpio_pin->dev &&
		    tmp->gpio_pin.pin == gpio_pin->pin) {
			tmp->use_cnt++;
			mtx_unlock(&gpio_list_mtx);
			free(entry, M_FIXEDREGULATOR);
			return (tmp);
		}
	}

	/* Reserve pin. */
	/* XXX Can we call gpiobus_acquire_pin() with gpio_list_mtx held? */
	rv = gpiobus_acquire_pin(busdev, gpio_pin->pin);
	if (rv != 0) {
		mtx_unlock(&gpio_list_mtx);
		free(entry, M_FIXEDREGULATOR);
		return (NULL);
	}
	/* Everything is OK, build new entry and insert it to list. */
	entry->gpio_pin = *gpio_pin;
	entry->use_cnt = 1;
	TAILQ_INSERT_TAIL(&gpio_list, entry, link);

	mtx_unlock(&gpio_list_mtx);
	return (entry);
}


/*
 * Regulator class implementation.
 */
static int
regnode_fixed_init(struct regnode *regnode)
{
	device_t dev;
	struct regnode_fixed_sc *sc;
	struct gpiobus_pin *pin;
	uint32_t flags;
	int rv;

	sc = regnode_get_softc(regnode);
	dev = regnode_get_device(regnode);
	sc->param = regnode_get_stdparam(regnode);
	if (sc->gpio_entry == NULL)
		return (0);
	pin = &sc->gpio_entry->gpio_pin;

	flags = GPIO_PIN_OUTPUT;
	if (sc->gpio_open_drain)
		flags |= GPIO_PIN_OPENDRAIN;
	if (sc->param->boot_on || sc->param->always_on) {
		rv = GPIO_PIN_SET(pin->dev, pin->pin, sc->param->enable_active_high);
		if (rv != 0) {
			device_printf(dev, "Cannot set GPIO pin: %d\n",
			    pin->pin);
			return (rv);
		}
	}

	rv = GPIO_PIN_SETFLAGS(pin->dev, pin->pin, flags);
	if (rv != 0) {
		device_printf(dev, "Cannot configure GPIO pin: %d\n", pin->pin);
		return (rv);
	}

	return (0);
}

/*
 * Enable/disable regulator.
 * Take shared GPIO pins in account
 */
static int
regnode_fixed_enable(struct regnode *regnode, bool enable, int *udelay)
{
	device_t dev;
	struct regnode_fixed_sc *sc;
	struct gpiobus_pin *pin;
	int rv;

	sc = regnode_get_softc(regnode);
	dev = regnode_get_device(regnode);

	*udelay = 0;
	if (sc->gpio_entry == NULL)
		return (0);
	pin = &sc->gpio_entry->gpio_pin;
	if (enable) {
		sc->gpio_entry->enable_cnt++;
		if (sc->gpio_entry->enable_cnt > 1)
			return (0);
	} else {
		KASSERT(sc->gpio_entry->enable_cnt > 0,
		    ("Invalid enable count"));
		sc->gpio_entry->enable_cnt--;
		if (sc->gpio_entry->enable_cnt >= 1)
			return (0);
	}
	if (sc->gpio_entry->always_on && !enable)
		return (0);
	if (!sc->param->enable_active_high)
		enable = !enable;
	rv = GPIO_PIN_SET(pin->dev, pin->pin, enable);
	if (rv != 0) {
		device_printf(dev, "Cannot set GPIO pin: %d\n", pin->pin);
		return (rv);
	}
	*udelay = sc->param->enable_delay;
	return (0);
}

/*
 * Stop (physicaly shutdown) regulator.
 * Take shared GPIO pins in account
 */
static int
regnode_fixed_stop(struct regnode *regnode, int *udelay)
{
	device_t dev;
	struct regnode_fixed_sc *sc;
	struct gpiobus_pin *pin;
	int rv;

	sc = regnode_get_softc(regnode);
	dev = regnode_get_device(regnode);

	*udelay = 0;
	if (sc->gpio_entry == NULL)
		return (0);
	if (sc->gpio_entry->always_on)
		return (0);
	pin = &sc->gpio_entry->gpio_pin;
	if (sc->gpio_entry->enable_cnt > 0) {
		/* Other regulator(s) are enabled. */
		/* XXXX Any diagnostic message? Or error? */
		return (0);
	}
	rv = GPIO_PIN_SET(pin->dev, pin->pin,
	    sc->param->enable_active_high ? false: true);
	if (rv != 0) {
		device_printf(dev, "Cannot set GPIO pin: %d\n", pin->pin);
		return (rv);
	}
	*udelay = sc->param->enable_delay;
	return (0);
}

static int
regnode_fixed_status(struct regnode *regnode, int *status)
{
	struct regnode_fixed_sc *sc;
	struct gpiobus_pin *pin;
	uint32_t val;
	int rv;

	sc = regnode_get_softc(regnode);

	*status = 0;
	if (sc->gpio_entry == NULL) {
		*status = REGULATOR_STATUS_ENABLED;
		return (0);
	}
	pin = &sc->gpio_entry->gpio_pin;

	rv = GPIO_PIN_GET(pin->dev, pin->pin, &val);
	if (rv == 0) {
		if (!sc->param->enable_active_high ^ (val != 0))
			*status = REGULATOR_STATUS_ENABLED;
	}
	return (rv);
}

int
regnode_fixed_register(device_t dev, struct regnode_fixed_init_def *init_def)
{
	struct regnode *regnode;
	struct regnode_fixed_sc *sc;

	regnode = regnode_create(dev, &regnode_fixed_class,
	    &init_def->reg_init_def);
	if (regnode == NULL) {
		device_printf(dev, "Cannot create regulator.\n");
		return(ENXIO);
	}
	sc = regnode_get_softc(regnode);
	sc->gpio_open_drain = init_def->gpio_open_drain;
	if (init_def->gpio_pin != NULL) {
		sc->gpio_entry = regnode_get_gpio_entry(init_def->gpio_pin);
		if (sc->gpio_entry == NULL)
			return(ENXIO);
	}
	regnode = regnode_register(regnode);
	if (regnode == NULL) {
		device_printf(dev, "Cannot register regulator.\n");
		return(ENXIO);
	}

	if (sc->gpio_entry != NULL)
		sc->gpio_entry->always_on |= sc->param->always_on;

	return (0);
}

/*
 * OFW Driver implementation.
 */
#ifdef FDT

struct  regfix_softc
{
	device_t			dev;
	bool				attach_done;
	struct regnode_fixed_init_def	init_def;
	phandle_t			gpio_prodxref;
	pcell_t				*gpio_cells;
	int				gpio_ncells;
	struct gpiobus_pin		gpio_pin;
};

static struct ofw_compat_data compat_data[] = {
	{"regulator-fixed",		1},
	{NULL,				0},
};

static int
regfix_get_gpio(struct regfix_softc * sc)
{
	device_t busdev;
	phandle_t node;

	int rv;

	if (sc->gpio_prodxref == 0)
		return (0);

	node = ofw_bus_get_node(sc->dev);

	/* Test if controller exist. */
	sc->gpio_pin.dev = OF_device_from_xref(sc->gpio_prodxref);
	if (sc->gpio_pin.dev == NULL)
		return (ENODEV);

	/* Test if GPIO bus already exist. */
	busdev = GPIO_GET_BUS(sc->gpio_pin.dev);
	if (busdev == NULL)
		return (ENODEV);

	rv = gpio_map_gpios(sc->gpio_pin.dev, node,
	    OF_node_from_xref(sc->gpio_prodxref), sc->gpio_ncells,
	    sc->gpio_cells, &(sc->gpio_pin.pin), &(sc->gpio_pin.flags));
	if (rv != 0) {
		device_printf(sc->dev, "Cannot map the gpio property.\n");
		return (ENXIO);
	}
	sc->init_def.gpio_pin = &sc->gpio_pin;
	return (0);
}

static int
regfix_parse_fdt(struct regfix_softc * sc)
{
	phandle_t node;
	int rv;
	struct regnode_init_def *init_def;

	node = ofw_bus_get_node(sc->dev);
	init_def = &sc->init_def.reg_init_def;

	rv = regulator_parse_ofw_stdparam(sc->dev, node, init_def);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot parse standard parameters.\n");
		return(rv);
	}

	/* Fixed regulator uses 'startup-delay-us' property for enable_delay */
	rv = OF_getencprop(node, "startup-delay-us",
	   &init_def->std_param.enable_delay,
	   sizeof(init_def->std_param.enable_delay));
	if (rv <= 0)
		init_def->std_param.enable_delay = 0;
	/* GPIO pin */
	if (OF_hasprop(node, "gpio-open-drain"))
		sc->init_def.gpio_open_drain = true;

	if (!OF_hasprop(node, "gpio"))
		return (0);
	rv = ofw_bus_parse_xref_list_alloc(node, "gpio", "#gpio-cells", 0,
	    &sc->gpio_prodxref, &sc->gpio_ncells, &sc->gpio_cells);
	if (rv != 0) {
		sc->gpio_prodxref = 0;
		device_printf(sc->dev, "Malformed gpio property\n");
		return (ENXIO);
	}
	return (0);
}

static void
regfix_new_pass(device_t dev)
{
	struct regfix_softc * sc;
	int rv;

	sc = device_get_softc(dev);
	bus_generic_new_pass(dev);

	if (sc->attach_done)
		return;

	/* Try to get and configure GPIO. */
	rv = regfix_get_gpio(sc);
	if (rv != 0)
		return;

	/* Register regulator. */
	regnode_fixed_register(sc->dev, &sc->init_def);
	sc->attach_done = true;
}

static int
regfix_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Fixed Regulator");
	return (BUS_PROBE_DEFAULT);
}

static int
regfix_detach(device_t dev)
{

	/* This device is always present. */
	return (EBUSY);
}

static int
regfix_attach(device_t dev)
{
	struct regfix_softc * sc;
	int rv;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Parse FDT data. */
	rv = regfix_parse_fdt(sc);
	if (rv != 0)
		return(ENXIO);

	/* Fill reset of init. */
	sc->init_def.reg_init_def.id = 1;
	sc->init_def.reg_init_def.flags = REGULATOR_FLAGS_STATIC;

	/* Try to get and configure GPIO. */
	rv = regfix_get_gpio(sc);
	if (rv != 0)
		return (bus_generic_attach(dev));

	/* Register regulator. */
	regnode_fixed_register(sc->dev, &sc->init_def);
	sc->attach_done = true;

	return (bus_generic_attach(dev));
}

static device_method_t regfix_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		regfix_probe),
	DEVMETHOD(device_attach,	regfix_attach),
	DEVMETHOD(device_detach,	regfix_detach),
	/* Bus interface */
	DEVMETHOD(bus_new_pass,		regfix_new_pass),
	/* Regdev interface */
	DEVMETHOD(regdev_map,		regdev_default_ofw_map),

	DEVMETHOD_END
};

static devclass_t regfix_devclass;
DEFINE_CLASS_0(regfix, regfix_driver, regfix_methods,
    sizeof(struct regfix_softc));
EARLY_DRIVER_MODULE(regfix, simplebus, regfix_driver,
   regfix_devclass, 0, 0, BUS_PASS_BUS);

#endif /* FDT */
