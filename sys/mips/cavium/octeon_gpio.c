/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * GPIO driver for Cavium Octeon 
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

#include <contrib/octeon-sdk/cvmx.h>
#include <contrib/octeon-sdk/cvmx-gpio.h>
#include <mips/cavium/octeon_irq.h>

#include <mips/cavium/octeon_gpiovar.h>
#include <dev/gpio/gpiobusvar.h>

#include "gpio_if.h"

#define	DEFAULT_CAPS	(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)

struct octeon_gpio_pin {
	const char *name;
	int pin;
	int flags;
};

/*
 * on CAP100 GPIO 7 is "Factory defaults" button
 *
 */
static struct octeon_gpio_pin octeon_gpio_pins[] = {
	{ "F/D", 7,  GPIO_PIN_INPUT},
	{ NULL, 0, 0},
};

/*
 * Helpers
 */
static void octeon_gpio_pin_configure(struct octeon_gpio_softc *sc, 
    struct gpio_pin *pin, uint32_t flags);

/*
 * Driver stuff
 */
static void octeon_gpio_identify(driver_t *, device_t);
static int octeon_gpio_probe(device_t dev);
static int octeon_gpio_attach(device_t dev);
static int octeon_gpio_detach(device_t dev);
static int octeon_gpio_filter(void *arg);
static void octeon_gpio_intr(void *arg);

/*
 * GPIO interface
 */
static device_t octeon_gpio_get_bus(device_t);
static int octeon_gpio_pin_max(device_t dev, int *maxpin);
static int octeon_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps);
static int octeon_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t
    *flags);
static int octeon_gpio_pin_getname(device_t dev, uint32_t pin, char *name);
static int octeon_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags);
static int octeon_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value);
static int octeon_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val);
static int octeon_gpio_pin_toggle(device_t dev, uint32_t pin);

static void
octeon_gpio_pin_configure(struct octeon_gpio_softc *sc, struct gpio_pin *pin,
    unsigned int flags)
{
	uint32_t mask;
	cvmx_gpio_bit_cfgx_t gpio_cfgx;

	mask = 1 << pin->gp_pin;
	GPIO_LOCK(sc);

	/*
	 * Manage input/output
	 */
	if (flags & (GPIO_PIN_INPUT|GPIO_PIN_OUTPUT)) {
		gpio_cfgx.u64 = cvmx_read_csr(CVMX_GPIO_BIT_CFGX(pin->gp_pin));
		pin->gp_flags &= ~(GPIO_PIN_INPUT|GPIO_PIN_OUTPUT);
		if (flags & GPIO_PIN_OUTPUT) {
			pin->gp_flags |= GPIO_PIN_OUTPUT;
			gpio_cfgx.s.tx_oe = 1;
		}
		else {
			pin->gp_flags |= GPIO_PIN_INPUT;
			gpio_cfgx.s.tx_oe = 0;
		}
		if (flags & GPIO_PIN_INVIN)
			gpio_cfgx.s.rx_xor = 1;
		else
			gpio_cfgx.s.rx_xor = 0;
		cvmx_write_csr(CVMX_GPIO_BIT_CFGX(pin->gp_pin), gpio_cfgx.u64);
	}

	GPIO_UNLOCK(sc);
}

static device_t
octeon_gpio_get_bus(device_t dev)
{
	struct octeon_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->busdev);
}

static int
octeon_gpio_pin_max(device_t dev, int *maxpin)
{

	*maxpin = OCTEON_GPIO_PINS - 1;
	return (0);
}

static int
octeon_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct octeon_gpio_softc *sc = device_get_softc(dev);
	int i;

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
octeon_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct octeon_gpio_softc *sc = device_get_softc(dev);
	int i;

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
octeon_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct octeon_gpio_softc *sc = device_get_softc(dev);
	int i;

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
octeon_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	int i;
	struct octeon_gpio_softc *sc = device_get_softc(dev);

	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	octeon_gpio_pin_configure(sc, &sc->gpio_pins[i], flags);

	return (0);
}

static int
octeon_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct octeon_gpio_softc *sc = device_get_softc(dev);
	int i;

	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	if (value)
		cvmx_gpio_set(1 << pin);
	else
		cvmx_gpio_clear(1 << pin);
	GPIO_UNLOCK(sc);

	return (0);
}

static int
octeon_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val)
{
	struct octeon_gpio_softc *sc = device_get_softc(dev);
	int i;
	uint64_t state;

	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	state = cvmx_gpio_read();
	*val = (state & (1 << pin)) ? 1 : 0;
	GPIO_UNLOCK(sc);

	return (0);
}

static int
octeon_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	int i;
	uint64_t state;
	struct octeon_gpio_softc *sc = device_get_softc(dev);

	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	/*
	 * XXX: Need to check if read returns actual state of output 
	 * pins or we need to keep this information by ourself
	 */
	state = cvmx_gpio_read();
	if (state & (1 << pin))
		cvmx_gpio_clear(1 << pin);
	else
		cvmx_gpio_set(1 << pin);
	GPIO_UNLOCK(sc);

	return (0);
}

static int
octeon_gpio_filter(void *arg)
{
	cvmx_gpio_bit_cfgx_t gpio_cfgx;
	void **cookie = arg;
	struct octeon_gpio_softc *sc = *cookie;
	long int irq = (cookie - sc->gpio_intr_cookies);
	
	if ((irq < 0) || (irq >= OCTEON_GPIO_IRQS))
		return (FILTER_STRAY);

	gpio_cfgx.u64 = cvmx_read_csr(CVMX_GPIO_BIT_CFGX(irq));
	/* Clear rising edge detector */
	if (gpio_cfgx.s.int_type == OCTEON_GPIO_IRQ_EDGE)
		cvmx_gpio_interrupt_clear(1 << irq);
	/* disable interrupt  */
	gpio_cfgx.s.int_en = 0;
	cvmx_write_csr(CVMX_GPIO_BIT_CFGX(irq), gpio_cfgx.u64);

	return (FILTER_SCHEDULE_THREAD);
}

static void
octeon_gpio_intr(void *arg)
{
	cvmx_gpio_bit_cfgx_t gpio_cfgx;
	void **cookie = arg;
	struct octeon_gpio_softc *sc = *cookie;
	long int irq = (cookie - sc->gpio_intr_cookies);

	if ((irq < 0) || (irq >= OCTEON_GPIO_IRQS)) {
		printf("%s: invalid GPIO IRQ: %ld\n", 
		    __func__, irq);
		return;
	}

	GPIO_LOCK(sc);
	gpio_cfgx.u64 = cvmx_read_csr(CVMX_GPIO_BIT_CFGX(irq));
	/* disable interrupt  */
	gpio_cfgx.s.int_en = 1;
	cvmx_write_csr(CVMX_GPIO_BIT_CFGX(irq), gpio_cfgx.u64);

	/* TODO: notify bus here or something */
	printf("GPIO IRQ for pin %ld\n", irq);
	GPIO_UNLOCK(sc);
}

static void
octeon_gpio_identify(driver_t *drv, device_t parent)
{

	BUS_ADD_CHILD(parent, 0, "gpio", 0);
}

static int
octeon_gpio_probe(device_t dev)
{

	device_set_desc(dev, "Cavium Octeon GPIO driver");
	return (0);
}

static int
octeon_gpio_attach(device_t dev)
{
	struct octeon_gpio_softc *sc = device_get_softc(dev);
	struct octeon_gpio_pin *pinp;
	cvmx_gpio_bit_cfgx_t gpio_cfgx;
	
	int i;

	KASSERT((device_get_unit(dev) == 0),
	    ("octeon_gpio: Only one gpio module supported"));

	mtx_init(&sc->gpio_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	for ( i = 0; i < OCTEON_GPIO_IRQS; i++) {
		if ((sc->gpio_irq_res[i] = bus_alloc_resource(dev, 
		    SYS_RES_IRQ, &sc->gpio_irq_rid[i], 
		    OCTEON_IRQ_GPIO0 + i, OCTEON_IRQ_GPIO0 + i, 1, 
		    RF_SHAREABLE | RF_ACTIVE)) == NULL) {
			device_printf(dev, "unable to allocate IRQ resource\n");
			octeon_gpio_detach(dev);
			return (ENXIO);
		}

		sc->gpio_intr_cookies[i] = sc;
		if ((bus_setup_intr(dev, sc->gpio_irq_res[i], INTR_TYPE_MISC, 
	    	    octeon_gpio_filter, octeon_gpio_intr, 
		    &(sc->gpio_intr_cookies[i]), &sc->gpio_ih[i]))) {
			device_printf(dev,
		    	"WARNING: unable to register interrupt handler\n");
			octeon_gpio_detach(dev);
			return (ENXIO);
		}
	}

	sc->dev = dev;
	/* Configure all pins as input */
	/* disable interrupts for all pins */
	pinp = octeon_gpio_pins;
	i = 0;
	while (pinp->name) {
		strncpy(sc->gpio_pins[i].gp_name, pinp->name, GPIOMAXNAME);
		sc->gpio_pins[i].gp_pin = pinp->pin;
		sc->gpio_pins[i].gp_caps = DEFAULT_CAPS;
		sc->gpio_pins[i].gp_flags = 0;
		octeon_gpio_pin_configure(sc, &sc->gpio_pins[i], pinp->flags);
		pinp++;
		i++;
	}

	sc->gpio_npins = i;

#if 0
	/*
	 * Sample: how to enable edge-triggered interrupt
	 * for GPIO pin
	 */
	gpio_cfgx.u64 = cvmx_read_csr(CVMX_GPIO_BIT_CFGX(7));
	gpio_cfgx.s.int_en = 1;
	gpio_cfgx.s.int_type = OCTEON_GPIO_IRQ_EDGE;
	cvmx_write_csr(CVMX_GPIO_BIT_CFGX(7), gpio_cfgx.u64);
#endif

	if (bootverbose) {
		for (i = 0; i < 16; i++) {
			gpio_cfgx.u64 = cvmx_read_csr(CVMX_GPIO_BIT_CFGX(i));
			device_printf(dev, "[pin%d] output=%d, invinput=%d, intr=%d, intr_type=%s\n", 
			    i, gpio_cfgx.s.tx_oe, gpio_cfgx.s.rx_xor, 
			    gpio_cfgx.s.int_en, gpio_cfgx.s.int_type ? "rising edge" : "level");
		}
	}
	sc->busdev = gpiobus_attach_bus(dev);
	if (sc->busdev == NULL) {
		octeon_gpio_detach(dev);
		return (ENXIO);
	}

	return (0);
}

static int
octeon_gpio_detach(device_t dev)
{
	struct octeon_gpio_softc *sc = device_get_softc(dev);
	int i;

	KASSERT(mtx_initialized(&sc->gpio_mtx), ("gpio mutex not initialized"));

	for ( i = 0; i < OCTEON_GPIO_IRQS; i++) {
		if (sc->gpio_ih[i])
			bus_teardown_intr(dev, sc->gpio_irq_res[i],
			    sc->gpio_ih[i]);
		if (sc->gpio_irq_res[i])
			bus_release_resource(dev, SYS_RES_IRQ,
			    sc->gpio_irq_rid[i], sc->gpio_irq_res[i]);
	}
	gpiobus_detach_bus(dev);
	mtx_destroy(&sc->gpio_mtx);

	return(0);
}

static device_method_t octeon_gpio_methods[] = {
	DEVMETHOD(device_identify, octeon_gpio_identify),
	DEVMETHOD(device_probe, octeon_gpio_probe),
	DEVMETHOD(device_attach, octeon_gpio_attach),
	DEVMETHOD(device_detach, octeon_gpio_detach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus, octeon_gpio_get_bus),
	DEVMETHOD(gpio_pin_max, octeon_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname, octeon_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags, octeon_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps, octeon_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags, octeon_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get, octeon_gpio_pin_get),
	DEVMETHOD(gpio_pin_set, octeon_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle, octeon_gpio_pin_toggle),
	{0, 0},
};

static driver_t octeon_gpio_driver = {
	"gpio",
	octeon_gpio_methods,
	sizeof(struct octeon_gpio_softc),
};
static devclass_t octeon_gpio_devclass;

DRIVER_MODULE(octeon_gpio, ciu, octeon_gpio_driver, octeon_gpio_devclass, 0, 0);
