/*-
 * Copyright (c) 2011 Jakub Wojciech Klama <jceel@FreeBSD.org>
 * Copyright (c) 2015 Hiroki Mori
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

/*
 * GPIO on RT1310A consist of 2 ports:
 * - PortA with 8 input/output pins
 * - PortB with 4 input/output pins
 *
 * Pins are mapped to logical pin number as follows:
 * [0..7] -> GPI_00..GPI_07 		(port A)
 * [8..11] -> GPI_08..GPI_11 		(port B)
 *
 */


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>
#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/resource.h>
#include <machine/intr.h>
#include <machine/fdt.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ralink/rt1310reg.h>
#include <arm/ralink/rt1310var.h>

#include "gpio_if.h"

struct rt1310_gpio_softc
{
	device_t		lg_dev;
	device_t		lg_busdev;
	struct resource *	lg_res;
	bus_space_tag_t		lg_bst;
	bus_space_handle_t	lg_bsh;
};

struct rt1310_gpio_pinmap
{
	int			lp_start_idx;
	int			lp_pin_count;
	int			lp_port;
	int			lp_start_bit;
	int			lp_flags;
};

static const struct rt1310_gpio_pinmap rt1310_gpio_pins[] = {
	{ 0,	8,	RT_GPIO_PORTA,	0,	GPIO_PIN_INPUT | GPIO_PIN_OUTPUT },
	{ 8,	4,	RT_GPIO_PORTB,	0,	GPIO_PIN_INPUT | GPIO_PIN_OUTPUT },
	{ -1,	-1,	-1,	-1,	-1 },
};

#define	RT_GPIO_NPINS				12

#define	RT_GPIO_PIN_IDX(_map, _idx)	\
    (_idx - _map->lp_start_idx)

#define	RT_GPIO_PIN_BIT(_map, _idx)	\
    (_map->lp_start_bit + RT_GPIO_PIN_IDX(_map, _idx))

static int rt1310_gpio_probe(device_t);
static int rt1310_gpio_attach(device_t);
static int rt1310_gpio_detach(device_t);

static device_t rt1310_gpio_get_bus(device_t);
static int rt1310_gpio_pin_max(device_t, int *);
static int rt1310_gpio_pin_getcaps(device_t, uint32_t, uint32_t *);
static int rt1310_gpio_pin_getflags(device_t, uint32_t, uint32_t *);
static int rt1310_gpio_pin_setflags(device_t, uint32_t, uint32_t);
static int rt1310_gpio_pin_getname(device_t, uint32_t, char *);
static int rt1310_gpio_pin_get(device_t, uint32_t, uint32_t *);
static int rt1310_gpio_pin_set(device_t, uint32_t, uint32_t);
static int rt1310_gpio_pin_toggle(device_t, uint32_t);

static const struct rt1310_gpio_pinmap *rt1310_gpio_get_pinmap(int);

static struct rt1310_gpio_softc *rt1310_gpio_sc = NULL;

#define	rt1310_gpio_read_4(_sc, _reg) \
    bus_space_read_4(_sc->lg_bst, _sc->lg_bsh, _reg)
#define	rt1310_gpio_write_4(_sc, _reg, _val) \
    bus_space_write_4(_sc->lg_bst, _sc->lg_bsh, _reg, _val)

static int
rt1310_gpio_probe(device_t dev)
{
	phandle_t node;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ralink,rt1310-gpio"))
		return (ENXIO);
		
	node = ofw_bus_get_node(dev);
	if (!OF_hasprop(node, "gpio-controller"))
		return (ENXIO);

	device_set_desc(dev, "RT1310 GPIO");
	return (BUS_PROBE_DEFAULT);
}

static int
rt1310_gpio_attach(device_t dev)
{
	struct rt1310_gpio_softc *sc = device_get_softc(dev);
	int rid;

	sc->lg_dev = dev;

	rid = 0;
	sc->lg_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->lg_res) {
		device_printf(dev, "cannot allocate memory window\n");
		return (ENXIO);
	}

	sc->lg_bst = rman_get_bustag(sc->lg_res);
	sc->lg_bsh = rman_get_bushandle(sc->lg_res);

	rt1310_gpio_sc = sc;

	sc->lg_busdev = gpiobus_attach_bus(dev);
	if (sc->lg_busdev == NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->lg_res);
		return (ENXIO);
	}

	return (0);
}

static int
rt1310_gpio_detach(device_t dev)
{
	return (EBUSY);
}

static device_t
rt1310_gpio_get_bus(device_t dev)
{
	struct rt1310_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->lg_busdev);
}

static int
rt1310_gpio_pin_max(device_t dev, int *npins)
{
	*npins = RT_GPIO_NPINS - 1;
	return (0);
}

static int
rt1310_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	const struct rt1310_gpio_pinmap *map;

	if (pin > RT_GPIO_NPINS)
		return (ENODEV);

	map = rt1310_gpio_get_pinmap(pin);

	*caps = map->lp_flags;
	return (0);
}

static int
rt1310_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct rt1310_gpio_softc *sc = device_get_softc(dev);
	const struct rt1310_gpio_pinmap *map;
	uint32_t state;
	int dir;

	if (pin > RT_GPIO_NPINS)
		return (ENODEV);

	map = rt1310_gpio_get_pinmap(pin);

	/* Check whether it's bidirectional pin */
	if ((map->lp_flags & (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)) != 
	    (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)) {
		*flags = map->lp_flags;
		return (0);
	}

	switch (map->lp_port) {
	case RT_GPIO_PORTA:
		state = rt1310_gpio_read_4(sc, RT_GPIO_OFF_PADIR);
		dir = (state & (1 << RT_GPIO_PIN_BIT(map, pin)));
		break;
	case RT_GPIO_PORTB:
		state = rt1310_gpio_read_4(sc, RT_GPIO_OFF_PBDIR);
		dir = (state & (1 << RT_GPIO_PIN_BIT(map, pin)));
		break;
	default:
		panic("unknown GPIO port");
	}

	*flags = dir ? GPIO_PIN_OUTPUT : GPIO_PIN_INPUT;

	return (0);
}

static int
rt1310_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct rt1310_gpio_softc *sc = device_get_softc(dev);
	const struct rt1310_gpio_pinmap *map;
	uint32_t dir, state;
	uint32_t port;

	if (pin > RT_GPIO_NPINS)
		return (ENODEV);

	map = rt1310_gpio_get_pinmap(pin);

	/* Check whether it's bidirectional pin */
	if ((map->lp_flags & (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)) != 
	    (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT))
		return (ENOTSUP);
	
	if (flags & GPIO_PIN_INPUT)
		dir = 0;

	if (flags & GPIO_PIN_OUTPUT)
		dir = 1;

	switch (map->lp_port) {
	case RT_GPIO_PORTA:
		port = RT_GPIO_OFF_PADIR;
		break;
	case RT_GPIO_PORTB:
		port = RT_GPIO_OFF_PBDIR;
		break;
	}

	state = rt1310_gpio_read_4(sc, port);
	if (flags & GPIO_PIN_INPUT) {
		state &= ~(1 << RT_GPIO_PIN_IDX(map, pin));
	} else {
		state |= (1 << RT_GPIO_PIN_IDX(map, pin));
	}
	rt1310_gpio_write_4(sc, port, state);

	return (0);
}

static int
rt1310_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	snprintf(name, GPIOMAXNAME - 1, "GPIO_%02d", pin);

	return (0);
}

static int
rt1310_gpio_pin_get(device_t dev, uint32_t pin, uint32_t *value)
{
	struct rt1310_gpio_softc *sc = device_get_softc(dev);
	const struct rt1310_gpio_pinmap *map;
	uint32_t state, flags;
	int dir;

	map = rt1310_gpio_get_pinmap(pin);

	if (rt1310_gpio_pin_getflags(dev, pin, &flags))
		return (ENXIO);

	if (flags & GPIO_PIN_OUTPUT)
		dir = 1;

	if (flags & GPIO_PIN_INPUT)
		dir = 0;

	switch (map->lp_port) {
	case RT_GPIO_PORTA:
		state = rt1310_gpio_read_4(sc, RT_GPIO_OFF_PADR);
		*value = !!(state & (1 << RT_GPIO_PIN_BIT(map, pin)));
		break;
	case RT_GPIO_PORTB:
		state = rt1310_gpio_read_4(sc, RT_GPIO_OFF_PBDR);
		*value = !!(state & (1 << RT_GPIO_PIN_BIT(map, pin)));
		break;
	}

	return (0);
}

static int
rt1310_gpio_pin_set(device_t dev, uint32_t pin, uint32_t value)
{
	struct rt1310_gpio_softc *sc = device_get_softc(dev);
	const struct rt1310_gpio_pinmap *map;
	uint32_t state, flags;
	uint32_t port;

	map = rt1310_gpio_get_pinmap(pin);

	if (rt1310_gpio_pin_getflags(dev, pin, &flags))
		return (ENXIO);

	if ((flags & GPIO_PIN_OUTPUT) == 0)
		return (EINVAL);

	switch (map->lp_port) {
	case RT_GPIO_PORTA:
		port = RT_GPIO_OFF_PADR;
		break;
	case RT_GPIO_PORTB:
		port = RT_GPIO_OFF_PBDR;
		break;
	}

	state = rt1310_gpio_read_4(sc, port);
	if (value == 1) {
		state |= (1 << RT_GPIO_PIN_BIT(map, pin));
	} else {
		state &= ~(1 << RT_GPIO_PIN_BIT(map, pin));
	}
	rt1310_gpio_write_4(sc, port, state);

	return (0);
}

static int
rt1310_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	const struct rt1310_gpio_pinmap *map;
	uint32_t flags;

	map = rt1310_gpio_get_pinmap(pin);

	if (rt1310_gpio_pin_getflags(dev, pin, &flags))
		return (ENXIO);

	if ((flags & GPIO_PIN_OUTPUT) == 0)
		return (EINVAL);
	
	panic("not implemented yet");

	return (0);

}

static const struct rt1310_gpio_pinmap *
rt1310_gpio_get_pinmap(int pin)
{
	const struct rt1310_gpio_pinmap *map;

	for (map = &rt1310_gpio_pins[0]; map->lp_start_idx != -1; map++) {
		if (pin >= map->lp_start_idx &&
		    pin < map->lp_start_idx + map->lp_pin_count)
			return map;
	}

	panic("pin number %d out of range", pin);
}

int
rt1310_gpio_set_flags(device_t dev, int pin, int flags)
{
	if (rt1310_gpio_sc == NULL)
		return (ENXIO);

	return rt1310_gpio_pin_setflags(rt1310_gpio_sc->lg_dev, pin, flags);
}

int
rt1310_gpio_set_state(device_t dev, int pin, int state)
{
	if (rt1310_gpio_sc == NULL)
		return (ENXIO);

	return rt1310_gpio_pin_set(rt1310_gpio_sc->lg_dev, pin, state); 
}

int
rt1310_gpio_get_state(device_t dev, int pin, int *state)
{
	if (rt1310_gpio_sc == NULL)
		return (ENXIO);

	return rt1310_gpio_pin_get(rt1310_gpio_sc->lg_dev, pin, state);
}

static phandle_t
rt1310_gpio_get_node(device_t bus, device_t dev)
{
	/* We only have one child, the GPIO bus, which needs our own node. */
	return (ofw_bus_get_node(bus));
}

static device_method_t rt1310_gpio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rt1310_gpio_probe),
	DEVMETHOD(device_attach,	rt1310_gpio_attach),
	DEVMETHOD(device_detach,	rt1310_gpio_detach),

	/* GPIO interface */
	DEVMETHOD(gpio_get_bus,		rt1310_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		rt1310_gpio_pin_max),
	DEVMETHOD(gpio_pin_getcaps,	rt1310_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_getflags,	rt1310_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_setflags,	rt1310_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_getname,	rt1310_gpio_pin_getname),
	DEVMETHOD(gpio_pin_set,		rt1310_gpio_pin_set),
	DEVMETHOD(gpio_pin_get,		rt1310_gpio_pin_get),
	DEVMETHOD(gpio_pin_toggle,	rt1310_gpio_pin_toggle),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	rt1310_gpio_get_node),

	{ 0, 0 }
};

static devclass_t rt1310_gpio_devclass;

static driver_t rt1310_gpio_driver = {
	"gpio",
	rt1310_gpio_methods,
	sizeof(struct rt1310_gpio_softc),
};

DRIVER_MODULE(rt1310gpio, simplebus, rt1310_gpio_driver, rt1310_gpio_devclass, 0, 0);
MODULE_VERSION(rt1310gpio, 1);
