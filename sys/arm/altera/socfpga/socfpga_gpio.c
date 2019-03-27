/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 * SOCFPGA General-Purpose I/O Interface.
 * Chapter 22, Cyclone V Device Handbook (CV-5V2 2014.07.22)
 */

/*
 * The GPIO modules are instances of the Synopsys® DesignWare® APB General
 * Purpose Programming I/O (DW_apb_gpio) peripheral.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>
#include <sys/mutex.h>
#include <sys/gpio.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include "gpio_if.h"

#define READ4(_sc, _reg) \
	bus_read_4((_sc)->res[0], _reg)
#define WRITE4(_sc, _reg, _val) \
	bus_write_4((_sc)->res[0], _reg, _val)

#define	GPIO_SWPORTA_DR		0x00	/* Port A Data Register */
#define	GPIO_SWPORTA_DDR	0x04	/* Port A Data Direction Register */
#define	GPIO_INTEN		0x30	/* Interrupt Enable Register */
#define	GPIO_INTMASK		0x34	/* Interrupt Mask Register */
#define	GPIO_INTTYPE_LEVEL	0x38	/* Interrupt Level Register */
#define	GPIO_INT_POLARITY	0x3C	/* Interrupt Polarity Register */
#define	GPIO_INTSTATUS		0x40	/* Interrupt Status Register */
#define	GPIO_RAW_INTSTATUS	0x44	/* Raw Interrupt Status Register */
#define	GPIO_DEBOUNCE		0x48	/* Debounce Enable Register */
#define	GPIO_PORTA_EOI		0x4C	/* Clear Interrupt Register */
#define	GPIO_EXT_PORTA		0x50	/* External Port A Register */
#define	GPIO_LS_SYNC		0x60	/* Synchronization Level Register */
#define	GPIO_ID_CODE		0x64	/* ID Code Register */
#define	GPIO_VER_ID_CODE	0x6C	/* GPIO Version Register */
#define	GPIO_CONFIG_REG2	0x70	/* Configuration Register 2 */
#define	 ENCODED_ID_PWIDTH_M	0x1f	/* Width of GPIO Port N Mask */
#define	 ENCODED_ID_PWIDTH_S(n)	(5 * n)	/* Width of GPIO Port N Shift */
#define	GPIO_CONFIG_REG1	0x74	/* Configuration Register 1 */

enum port_no {
	PORTA,
	PORTB,
	PORTC,
	PORTD,
};

#define	NR_GPIO_MAX	32	/* Maximum pins per port */

#define	GPIO_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	GPIO_UNLOCK(_sc)	mtx_unlock(&(_sc)->sc_mtx)

#define	DEFAULT_CAPS	(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)

/*
 * GPIO interface
 */
static device_t socfpga_gpio_get_bus(device_t);
static int socfpga_gpio_pin_max(device_t, int *);
static int socfpga_gpio_pin_getcaps(device_t, uint32_t, uint32_t *);
static int socfpga_gpio_pin_getname(device_t, uint32_t, char *);
static int socfpga_gpio_pin_getflags(device_t, uint32_t, uint32_t *);
static int socfpga_gpio_pin_setflags(device_t, uint32_t, uint32_t);
static int socfpga_gpio_pin_set(device_t, uint32_t, unsigned int);
static int socfpga_gpio_pin_get(device_t, uint32_t, unsigned int *);
static int socfpga_gpio_pin_toggle(device_t, uint32_t pin);

struct socfpga_gpio_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;

	device_t		dev;
	device_t		busdev;
	struct mtx		sc_mtx;
	int			gpio_npins;
	struct gpio_pin		gpio_pins[NR_GPIO_MAX];
};

struct socfpga_gpio_softc *gpio_sc;

static struct resource_spec socfpga_gpio_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
socfpga_gpio_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "snps,dw-apb-gpio"))
		return (ENXIO);

	device_set_desc(dev, "DesignWare General-Purpose I/O Interface");
	return (BUS_PROBE_DEFAULT);
}

static int
socfpga_gpio_attach(device_t dev)
{
	struct socfpga_gpio_softc *sc;
	int version;
	int nr_pins;
	int cfg2;
	int i;

	sc = device_get_softc(dev);
	sc->dev = dev;
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	if (bus_alloc_resources(dev, socfpga_gpio_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	gpio_sc = sc;

	version =  READ4(sc, GPIO_VER_ID_CODE);
#if 0
	device_printf(sc->dev, "Version = 0x%08x\n", version);
#endif

	/*
	 * Take number of pins from hardware.
	 * XXX: Assume we have GPIO port A only.
	 */
	cfg2 = READ4(sc, GPIO_CONFIG_REG2);
	nr_pins = (cfg2 >> ENCODED_ID_PWIDTH_S(PORTA)) & \
			ENCODED_ID_PWIDTH_M;
	sc->gpio_npins = nr_pins + 1;

	for (i = 0; i < sc->gpio_npins; i++) {
		sc->gpio_pins[i].gp_pin = i;
		sc->gpio_pins[i].gp_caps = DEFAULT_CAPS;
		sc->gpio_pins[i].gp_flags =
		    (READ4(sc, GPIO_SWPORTA_DDR) & (1 << i)) ?
		    GPIO_PIN_OUTPUT: GPIO_PIN_INPUT;
		snprintf(sc->gpio_pins[i].gp_name, GPIOMAXNAME,
		    "socfpga_gpio%d.%d", device_get_unit(dev), i);
	}
	sc->busdev = gpiobus_attach_bus(dev);
	if (sc->busdev == NULL) {
		bus_release_resources(dev, socfpga_gpio_spec, sc->res);
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}

	return (0);
}

static device_t
socfpga_gpio_get_bus(device_t dev)
{
	struct socfpga_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->busdev);
}

static int
socfpga_gpio_pin_max(device_t dev, int *maxpin)
{
	struct socfpga_gpio_softc *sc;

	sc = device_get_softc(dev);

	*maxpin = sc->gpio_npins - 1;

	return (0);
}

static int
socfpga_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct socfpga_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	memcpy(name, sc->gpio_pins[i].gp_name, GPIOMAXNAME);
	GPIO_UNLOCK(sc);

	return (0);
}

static int
socfpga_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct socfpga_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	*caps = sc->gpio_pins[i].gp_caps;
	GPIO_UNLOCK(sc);

	return (0);
}

static int
socfpga_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct socfpga_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	*flags = sc->gpio_pins[i].gp_flags;
	GPIO_UNLOCK(sc);

	return (0);
}

static int
socfpga_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val)
{
	struct socfpga_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	*val = (READ4(sc, GPIO_EXT_PORTA) & (1 << i)) ? 1 : 0;
	GPIO_UNLOCK(sc);

	return (0);
}

static int
socfpga_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct socfpga_gpio_softc *sc;
	int reg;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	reg = READ4(sc, GPIO_SWPORTA_DR);
	if (reg & (1 << i))
		reg &= ~(1 << i);
	else
		reg |= (1 << i);
	WRITE4(sc, GPIO_SWPORTA_DR, reg);
	GPIO_UNLOCK(sc);

	return (0);
}


static void
socfpga_gpio_pin_configure(struct socfpga_gpio_softc *sc,
    struct gpio_pin *pin, unsigned int flags)
{
	int reg;

	GPIO_LOCK(sc);

	/*
	 * Manage input/output
	 */

	reg = READ4(sc, GPIO_SWPORTA_DDR);
	if (flags & (GPIO_PIN_INPUT|GPIO_PIN_OUTPUT)) {
		pin->gp_flags &= ~(GPIO_PIN_INPUT|GPIO_PIN_OUTPUT);
		if (flags & GPIO_PIN_OUTPUT) {
			pin->gp_flags |= GPIO_PIN_OUTPUT;
			reg |= (1 << pin->gp_pin);
		} else {
			pin->gp_flags |= GPIO_PIN_INPUT;
			reg &= ~(1 << pin->gp_pin);
		}
	}

	WRITE4(sc, GPIO_SWPORTA_DDR, reg);
	GPIO_UNLOCK(sc);
}


static int
socfpga_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct socfpga_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	socfpga_gpio_pin_configure(sc, &sc->gpio_pins[i], flags);

	return (0);
}

static int
socfpga_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct socfpga_gpio_softc *sc;
	int reg;
	int i;

	sc = device_get_softc(dev);

	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	reg = READ4(sc, GPIO_SWPORTA_DR);
	if (value)
		reg |= (1 << i);
	else
		reg &= ~(1 << i);
	WRITE4(sc, GPIO_SWPORTA_DR, reg);
	GPIO_UNLOCK(sc);

	return (0);
}

static device_method_t socfpga_gpio_methods[] = {
	DEVMETHOD(device_probe,		socfpga_gpio_probe),
	DEVMETHOD(device_attach,	socfpga_gpio_attach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus,		socfpga_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		socfpga_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	socfpga_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getcaps,	socfpga_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_getflags,	socfpga_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_get,		socfpga_gpio_pin_get),
	DEVMETHOD(gpio_pin_toggle,	socfpga_gpio_pin_toggle),
	DEVMETHOD(gpio_pin_setflags,	socfpga_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_set,		socfpga_gpio_pin_set),
	{ 0, 0 }
};

static driver_t socfpga_gpio_driver = {
	"gpio",
	socfpga_gpio_methods,
	sizeof(struct socfpga_gpio_softc),
};

static devclass_t socfpga_gpio_devclass;

DRIVER_MODULE(socfpga_gpio, simplebus, socfpga_gpio_driver,
    socfpga_gpio_devclass, 0, 0);
