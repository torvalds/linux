/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * GPIO controlled regulators
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/gpio.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/gpio/gpiobusvar.h>

#include <dev/extres/regulator/regulator.h>

#include "regdev_if.h"

struct gpioregulator_state {
	int			val;
	uint32_t		mask;
};

struct gpioregulator_init_def {
	struct regnode_init_def		reg_init_def;
	struct gpiobus_pin		*enable_pin;
	int				enable_pin_valid;
	int				startup_delay_us;
	int				nstates;
	struct gpioregulator_state	*states;
	int				npins;
	struct gpiobus_pin		**pins;
};

struct gpioregulator_reg_sc {
	struct regnode			*regnode;
	device_t			base_dev;
	struct regnode_std_param	*param;
	struct gpioregulator_init_def	*def;
};

struct gpioregulator_softc {
	device_t			dev;
	struct gpioregulator_reg_sc	*reg_sc;
	struct gpioregulator_init_def	init_def;
};

static int
gpioregulator_regnode_init(struct regnode *regnode)
{
	struct gpioregulator_reg_sc *sc;
	int error, n;

	sc = regnode_get_softc(regnode);

	if (sc->def->enable_pin_valid == 1) {
		error = gpio_pin_setflags(sc->def->enable_pin, GPIO_PIN_OUTPUT);
		if (error != 0)
			return (error);
	}

	for (n = 0; n < sc->def->npins; n++) {
		error = gpio_pin_setflags(sc->def->pins[n], GPIO_PIN_OUTPUT);
		if (error != 0)
			return (error);
	}

	return (0);
}

static int
gpioregulator_regnode_enable(struct regnode *regnode, bool enable, int *udelay)
{
	struct gpioregulator_reg_sc *sc;
	bool active;
	int error;

	sc = regnode_get_softc(regnode);

	if (sc->def->enable_pin_valid == 1) {
		active = enable;
		if (!sc->param->enable_active_high)
			active = !active;
		error = gpio_pin_set_active(sc->def->enable_pin, active);
		if (error != 0)
			return (error);
	}

	*udelay = sc->def->startup_delay_us;

	return (0);
}

static int
gpioregulator_regnode_set_voltage(struct regnode *regnode, int min_uvolt,
    int max_uvolt, int *udelay)
{
	struct gpioregulator_reg_sc *sc;
	const struct gpioregulator_state *state;
	int error, n;

	sc = regnode_get_softc(regnode);
	state = NULL;

	for (n = 0; n < sc->def->nstates; n++) {
		if (sc->def->states[n].val >= min_uvolt &&
		    sc->def->states[n].val <= max_uvolt) {
			state = &sc->def->states[n];
			break;
		}
	}
	if (state == NULL)
		return (EINVAL);

	for (n = 0; n < sc->def->npins; n++) {
		error = gpio_pin_set_active(sc->def->pins[n],
		    (state->mask >> n) & 1);
		if (error != 0)
			return (error);
	}

	*udelay = sc->def->startup_delay_us;

	return (0);
}

static int
gpioregulator_regnode_get_voltage(struct regnode *regnode, int *uvolt)
{
	struct gpioregulator_reg_sc *sc;
	uint32_t mask;
	int error, n;
	bool active;

	sc = regnode_get_softc(regnode);
	mask = 0;

	for (n = 0; n < sc->def->npins; n++) {
		error = gpio_pin_is_active(sc->def->pins[n], &active);
		if (error != 0)
			return (error);
		mask |= (active << n);
	}

	for (n = 0; n < sc->def->nstates; n++) {
		if (sc->def->states[n].mask == mask) {
			*uvolt = sc->def->states[n].val;
			return (0);
		}
	}

	return (EIO);
}

static regnode_method_t gpioregulator_regnode_methods[] = {
	/* Regulator interface */
	REGNODEMETHOD(regnode_init,	gpioregulator_regnode_init),
	REGNODEMETHOD(regnode_enable,	gpioregulator_regnode_enable),
	REGNODEMETHOD(regnode_set_voltage, gpioregulator_regnode_set_voltage),
	REGNODEMETHOD(regnode_get_voltage, gpioregulator_regnode_get_voltage),
	REGNODEMETHOD_END
};
DEFINE_CLASS_1(gpioregulator_regnode, gpioregulator_regnode_class,
    gpioregulator_regnode_methods, sizeof(struct gpioregulator_reg_sc),
    regnode_class);

static int
gpioregulator_parse_fdt(struct gpioregulator_softc *sc)
{
	uint32_t *pstates, mask;
	phandle_t node;
	ssize_t len;
	int error, n;

	node = ofw_bus_get_node(sc->dev);
	pstates = NULL;
	mask = 0;

	error = regulator_parse_ofw_stdparam(sc->dev, node,
	    &sc->init_def.reg_init_def);
	if (error != 0)
		return (error);

	/* "states" property (required) */
	len = OF_getencprop_alloc_multi(node, "states", sizeof(*pstates),
	    (void **)&pstates);
	if (len < 2) {
		device_printf(sc->dev, "invalid 'states' property\n");
		error = EINVAL;
		goto done;
	}
	sc->init_def.nstates = len / 2;
	sc->init_def.states = malloc(sc->init_def.nstates *
	    sizeof(*sc->init_def.states), M_DEVBUF, M_WAITOK);
	for (n = 0; n < sc->init_def.nstates; n++) {
		sc->init_def.states[n].val = pstates[n * 2 + 0];
		sc->init_def.states[n].mask = pstates[n * 2 + 1];
		mask |= sc->init_def.states[n].mask;
	}

	/* "startup-delay-us" property (optional) */
	len = OF_getencprop(node, "startup-delay-us",
	    &sc->init_def.startup_delay_us,
	    sizeof(sc->init_def.startup_delay_us));
	if (len <= 0)
		sc->init_def.startup_delay_us = 0;

	/* "enable-gpio" property (optional) */
	error = gpio_pin_get_by_ofw_property(sc->dev, node, "enable-gpio",
	    &sc->init_def.enable_pin);
	if (error == 0)
		sc->init_def.enable_pin_valid = 1;

	/* "gpios" property */
	sc->init_def.npins = 32 - __builtin_clz(mask);
	sc->init_def.pins = malloc(sc->init_def.npins *
	    sizeof(sc->init_def.pins), M_DEVBUF, M_WAITOK);
	for (n = 0; n < sc->init_def.npins; n++) {
		error = gpio_pin_get_by_ofw_idx(sc->dev, node, n,
		    &sc->init_def.pins[n]);
		if (error != 0) {
			device_printf(sc->dev, "cannot get pin %d\n", n);
			goto done;
		}
	}

done:
	if (error != 0) {
		for (n = 0; n < sc->init_def.npins; n++) {
			if (sc->init_def.pins[n] != NULL)
				gpio_pin_release(sc->init_def.pins[n]);
		}

		free(sc->init_def.states, M_DEVBUF);
		free(sc->init_def.pins, M_DEVBUF);

	}
	OF_prop_free(pstates);

	return (error);
}

static int
gpioregulator_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "regulator-gpio"))
		return (ENXIO);

	device_set_desc(dev, "GPIO controlled regulator");
	return (BUS_PROBE_GENERIC);
}

static int
gpioregulator_attach(device_t dev)
{
	struct gpioregulator_softc *sc;
	struct regnode *regnode;
	phandle_t node;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	error = gpioregulator_parse_fdt(sc);
	if (error != 0) {
		device_printf(dev, "cannot parse parameters\n");
		return (ENXIO);
	}
	sc->init_def.reg_init_def.id = 1;
	sc->init_def.reg_init_def.ofw_node = node;

	regnode = regnode_create(dev, &gpioregulator_regnode_class,
	    &sc->init_def.reg_init_def);
	if (regnode == NULL) {
		device_printf(dev, "cannot create regulator\n");
		return (ENXIO);
	}

	sc->reg_sc = regnode_get_softc(regnode);
	sc->reg_sc->regnode = regnode;
	sc->reg_sc->base_dev = dev;
	sc->reg_sc->param = regnode_get_stdparam(regnode);
	sc->reg_sc->def = &sc->init_def;

	regnode_register(regnode);

	return (0);
}


static device_method_t gpioregulator_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		gpioregulator_probe),
	DEVMETHOD(device_attach,	gpioregulator_attach),

	/* Regdev interface */
	DEVMETHOD(regdev_map,		regdev_default_ofw_map),

	DEVMETHOD_END
};

static driver_t gpioregulator_driver = {
	"gpioregulator",
	gpioregulator_methods,
	sizeof(struct gpioregulator_softc),
};

static devclass_t gpioregulator_devclass;

EARLY_DRIVER_MODULE(gpioregulator, simplebus, gpioregulator_driver,
    gpioregulator_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LAST);
MODULE_VERSION(gpioregulator, 1);
