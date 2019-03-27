/*-
 * Copyright 2015 Alexander Kabaev <kan@FreeBSD.org>
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
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/gpio/gpiobusvar.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <mips/ingenic/jz4780_regs.h>
#include <gnu/dts/include/dt-bindings/interrupt-controller/irq.h>

#include "jz4780_gpio_if.h"
#include "gpio_if.h"
#include "pic_if.h"

#define JZ4780_GPIO_PINS 32

enum pin_function {
	JZ_FUNC_DEV_0,
	JZ_FUNC_DEV_1,
	JZ_FUNC_DEV_2,
	JZ_FUNC_DEV_3,
	JZ_FUNC_GPIO,
	JZ_FUNC_INTR,
};

struct jz4780_gpio_pin {
	struct intr_irqsrc pin_irqsrc;
	enum intr_trigger intr_trigger;
	enum intr_polarity intr_polarity;
	enum pin_function pin_func;
	uint32_t pin_caps;
	uint32_t pin_flags;
	uint32_t pin_num;
	char pin_name[GPIOMAXNAME];
};

struct jz4780_gpio_softc {
	device_t		dev;
	device_t		busdev;
	struct resource		*res[2];
	struct mtx		mtx;
	struct jz4780_gpio_pin  pins[JZ4780_GPIO_PINS];
	void			*intrhand;
};

static struct resource_spec jz4780_gpio_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ SYS_RES_IRQ,    0, RF_ACTIVE },
	{ -1, 0 }
};

static int jz4780_gpio_probe(device_t dev);
static int jz4780_gpio_attach(device_t dev);
static int jz4780_gpio_detach(device_t dev);
static int jz4780_gpio_intr(void *arg);

#define	JZ4780_GPIO_LOCK(sc)		mtx_lock_spin(&(sc)->mtx)
#define	JZ4780_GPIO_UNLOCK(sc)		mtx_unlock_spin(&(sc)->mtx)
#define	JZ4780_GPIO_LOCK_INIT(sc)	\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "jz4780_gpio", MTX_SPIN)
#define	JZ4780_GPIO_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx);

#define CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], (reg), (val))
#define CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], (reg))

static int
jz4780_gpio_probe(device_t dev)
{
	phandle_t node;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	/* We only like particular parent */
	if (!ofw_bus_is_compatible(device_get_parent(dev),
	   "ingenic,jz4780-pinctrl"))
		return (ENXIO);

	/* ... and only specific children os that parent */
	node = ofw_bus_get_node(dev);
	if (!OF_hasprop(node, "gpio-controller"))
		return (ENXIO);

	device_set_desc(dev, "Ingenic JZ4780 GPIO Controller");

	return (BUS_PROBE_DEFAULT);
}

static int
jz4780_gpio_pin_set_func(struct jz4780_gpio_softc *sc, uint32_t pin,
    uint32_t func)
{
	uint32_t mask = (1u << pin);

	if (func > (uint32_t)JZ_FUNC_DEV_3)
		return (EINVAL);

	CSR_WRITE_4(sc, JZ_GPIO_INTC, mask);
	CSR_WRITE_4(sc, JZ_GPIO_MASKC, mask);
	if (func & 2)
		CSR_WRITE_4(sc, JZ_GPIO_PAT1S, mask);
	else
		CSR_WRITE_4(sc, JZ_GPIO_PAT1C, mask);
	if (func & 1)
		CSR_WRITE_4(sc, JZ_GPIO_PAT0S, mask);
	else
		CSR_WRITE_4(sc, JZ_GPIO_PAT0C, mask);

	sc->pins[pin].pin_flags &= ~(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT);
	sc->pins[pin].pin_func = (enum pin_function)func;
	return (0);
}

static int
jz4780_gpio_pin_set_direction(struct jz4780_gpio_softc *sc,
    uint32_t pin, uint32_t dir)
{
	uint32_t mask = (1u << pin);

	switch (dir) {
	case GPIO_PIN_OUTPUT:
		if (sc->pins[pin].pin_caps & dir)
			CSR_WRITE_4(sc, JZ_GPIO_PAT1C, mask);
		else
			return (EINVAL);
		break;
	case GPIO_PIN_INPUT:
		if (sc->pins[pin].pin_caps & dir)
			CSR_WRITE_4(sc, JZ_GPIO_PAT1S, mask);
		else
			return (EINVAL);
		break;
	}

	sc->pins[pin].pin_flags &= ~(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT);
	sc->pins[pin].pin_flags |= dir;
	return (0);
}

static int
jz4780_gpio_pin_set_bias(struct jz4780_gpio_softc *sc,
    uint32_t pin, uint32_t bias)
{
	uint32_t mask = (1u << pin);

	switch (bias) {
	case GPIO_PIN_PULLUP:
	case GPIO_PIN_PULLDOWN:
		if (sc->pins[pin].pin_caps & bias)
			CSR_WRITE_4(sc, JZ_GPIO_DPULLC, mask);
		else
			return (EINVAL);
		break;
	case 0:
		CSR_WRITE_4(sc, JZ_GPIO_DPULLS, mask);
		break;
	default:
		return (ENOTSUP);
	}

	sc->pins[pin].pin_flags &= ~(GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN);
	sc->pins[pin].pin_flags |= bias;
	return (0);
}

/*
 * Decode pin configuration using this map
 */
#if 0
INT MASK PAT1 PAT0
1   x    0    0 /* intr, level, low */
1   x    0    1 /* intr, level, high */
1   x    1    0 /* intr, edge, falling */
1   x    1    1 /* intr, edge, rising */
0   0    0    0 /* function, func 0 */
0   0    0    1 /* function, func 1 */
0   0    1    0 /* function, func 2 */
0   0    1    0 /* function, func 3 */
0   1    0    0 /* gpio, output 0 */
0   1    0    1 /* gpio, output 1 */
0   1    1    x /* gpio, input */
#endif

static void
jz4780_gpio_pin_probe(struct jz4780_gpio_softc *sc, uint32_t pin)
{
	uint32_t mask = (1u << pin);
	uint32_t val;

	/* Clear cached gpio config */
	sc->pins[pin].pin_flags = 0;

	/* First check if pin is in interrupt mode */
	val = CSR_READ_4(sc, JZ_GPIO_INT);
	if (val & mask) {
		/* Pin is in interrupt mode, decode interrupt triggering mode */
		val = CSR_READ_4(sc, JZ_GPIO_PAT1);
		if (val & mask)
			sc->pins[pin].intr_trigger = INTR_TRIGGER_EDGE;
		else
			sc->pins[pin].intr_trigger = INTR_TRIGGER_LEVEL;
		/* Decode interrupt polarity */
		val = CSR_READ_4(sc, JZ_GPIO_PAT0);
		if (val & mask)
			sc->pins[pin].intr_polarity = INTR_POLARITY_HIGH;
		else
			sc->pins[pin].intr_polarity = INTR_POLARITY_LOW;

		sc->pins[pin].pin_func = JZ_FUNC_INTR;
		sc->pins[pin].pin_flags = 0;
		return;
	}
	/* Next check if pin is in gpio mode */
	val = CSR_READ_4(sc, JZ_GPIO_MASK);
	if (val & mask) {
		/* Pin is in gpio mode, decode direction and bias */
		val = CSR_READ_4(sc, JZ_GPIO_PAT1);
		if (val & mask)
			sc->pins[pin].pin_flags |= GPIO_PIN_INPUT;
		else
			sc->pins[pin].pin_flags |= GPIO_PIN_OUTPUT;
		/* Check for bias */
		val = CSR_READ_4(sc, JZ_GPIO_DPULL);
		if ((val & mask) == 0)
			sc->pins[pin].pin_flags |= sc->pins[pin].pin_caps &
				(GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN);
		sc->pins[pin].pin_func = JZ_FUNC_GPIO;
		return;
	}
	/* By exclusion, pin is in alternate function mode */
	val = CSR_READ_4(sc, JZ_GPIO_DPULL);
	if ((val & mask) == 0)
		sc->pins[pin].pin_flags = sc->pins[pin].pin_caps &
			(GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN);
	val = ((CSR_READ_4(sc, JZ_GPIO_PAT1) & mask) >> pin) << 1;
	val = val | ((CSR_READ_4(sc, JZ_GPIO_PAT1) & mask) >> pin);
	sc->pins[pin].pin_func = (enum pin_function)val;
}

static int
jz4780_gpio_register_isrcs(struct jz4780_gpio_softc *sc)
{
	int error;
	uint32_t irq, i;
	struct intr_irqsrc *isrc;
	const char *name;

	name = device_get_nameunit(sc->dev);
	for (irq = 0; irq < JZ4780_GPIO_PINS; irq++) {
		isrc = &sc->pins[irq].pin_irqsrc;
		error = intr_isrc_register(isrc, sc->dev, 0, "%s,%d",
		    name, irq);
		if (error != 0) {
			for (i = 0; i < irq; i++)
				intr_isrc_deregister(&sc->pins[i].pin_irqsrc);
			device_printf(sc->dev, "%s failed", __func__);
			return (error);
		}
	}

	return (0);
}

static int
jz4780_gpio_attach(device_t dev)
{
	struct jz4780_gpio_softc *sc = device_get_softc(dev);
	phandle_t node;
	uint32_t i, pd_pins, pu_pins;

	sc->dev = dev;

	if (bus_alloc_resources(dev, jz4780_gpio_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	JZ4780_GPIO_LOCK_INIT(sc);

	node = ofw_bus_get_node(dev);
	OF_getencprop(node, "ingenic,pull-ups", &pu_pins, sizeof(pu_pins));
	OF_getencprop(node, "ingenic,pull-downs", &pd_pins, sizeof(pd_pins));

	for (i = 0; i < JZ4780_GPIO_PINS; i++) {
		sc->pins[i].pin_num = i;
		sc->pins[i].pin_caps |= GPIO_PIN_INPUT | GPIO_PIN_OUTPUT;
		if (pu_pins & (1 << i))
			sc->pins[i].pin_caps |= GPIO_PIN_PULLUP;
		if (pd_pins & (1 << i))
			sc->pins[i].pin_caps |= GPIO_PIN_PULLDOWN;
		sc->pins[i].intr_polarity = INTR_POLARITY_CONFORM;
		sc->pins[i].intr_trigger = INTR_TRIGGER_CONFORM;

		snprintf(sc->pins[i].pin_name, GPIOMAXNAME - 1, "gpio%c%d",
		    device_get_unit(dev) + 'a', i);
		sc->pins[i].pin_name[GPIOMAXNAME - 1] = '\0';

		jz4780_gpio_pin_probe(sc, i);
	}

	if (jz4780_gpio_register_isrcs(sc) != 0)
		goto fail;

	if (intr_pic_register(dev, OF_xref_from_node(node)) == NULL) {
		device_printf(dev, "could not register PIC\n");
		goto fail;
	}

	if (bus_setup_intr(dev, sc->res[1], INTR_TYPE_MISC | INTR_MPSAFE,
	    jz4780_gpio_intr, NULL, sc, &sc->intrhand) != 0)
		goto fail_pic;

	sc->busdev = gpiobus_attach_bus(dev);
	if (sc->busdev == NULL)
		goto fail_pic;

	return (0);
fail_pic:
	intr_pic_deregister(dev, OF_xref_from_node(node));
fail:
	if (sc->intrhand != NULL)
		bus_teardown_intr(dev, sc->res[1], sc->intrhand);
	bus_release_resources(dev, jz4780_gpio_spec, sc->res);
	JZ4780_GPIO_LOCK_DESTROY(sc);
	return (ENXIO);
}

static int
jz4780_gpio_detach(device_t dev)
{
	struct jz4780_gpio_softc *sc = device_get_softc(dev);

	bus_release_resources(dev, jz4780_gpio_spec, sc->res);
	JZ4780_GPIO_LOCK_DESTROY(sc);
	return (0);
}

static int
jz4780_gpio_configure_pin(device_t dev, uint32_t pin, uint32_t func,
    uint32_t flags)
{
	struct jz4780_gpio_softc *sc;
	int retval;

	if (pin >= JZ4780_GPIO_PINS)
		return (EINVAL);

	sc = device_get_softc(dev);
	JZ4780_GPIO_LOCK(sc);
	retval = jz4780_gpio_pin_set_func(sc, pin, func);
	if (retval == 0)
		retval = jz4780_gpio_pin_set_bias(sc, pin, flags);
	JZ4780_GPIO_UNLOCK(sc);
	return (retval);
}

static device_t
jz4780_gpio_get_bus(device_t dev)
{
	struct jz4780_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->busdev);
}

static int
jz4780_gpio_pin_max(device_t dev, int *maxpin)
{

	*maxpin = JZ4780_GPIO_PINS - 1;
	return (0);
}

static int
jz4780_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct jz4780_gpio_softc *sc;

	if (pin >= JZ4780_GPIO_PINS)
		return (EINVAL);

	sc = device_get_softc(dev);
	JZ4780_GPIO_LOCK(sc);
	*caps = sc->pins[pin].pin_caps;
	JZ4780_GPIO_UNLOCK(sc);

	return (0);
}

static int
jz4780_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct jz4780_gpio_softc *sc;

	if (pin >= JZ4780_GPIO_PINS)
		return (EINVAL);

	sc = device_get_softc(dev);
	JZ4780_GPIO_LOCK(sc);
	*flags = sc->pins[pin].pin_flags;
	JZ4780_GPIO_UNLOCK(sc);

	return (0);
}

static int
jz4780_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct jz4780_gpio_softc *sc;

	if (pin >= JZ4780_GPIO_PINS)
		return (EINVAL);

	sc = device_get_softc(dev);
	strncpy(name, sc->pins[pin].pin_name, GPIOMAXNAME - 1);
	name[GPIOMAXNAME - 1] = '\0';

	return (0);
}

static int
jz4780_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct jz4780_gpio_softc *sc;
	int retval;

	if (pin >= JZ4780_GPIO_PINS)
		return (EINVAL);

	sc = device_get_softc(dev);
	JZ4780_GPIO_LOCK(sc);
	retval = jz4780_gpio_pin_set_direction(sc, pin,
	    flags & (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT));
	if (retval == 0)
		retval = jz4780_gpio_pin_set_bias(sc, pin,
		    flags & (GPIO_PIN_PULLDOWN | GPIO_PIN_PULLUP));
	JZ4780_GPIO_UNLOCK(sc);

	return (retval);
}

static int
jz4780_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct jz4780_gpio_softc *sc;
	uint32_t mask;
	int retval;

	if (pin >= JZ4780_GPIO_PINS)
		return (EINVAL);

	retval = EINVAL;
	mask = (1u << pin);
	sc = device_get_softc(dev);
	JZ4780_GPIO_LOCK(sc);
	if (sc->pins[pin].pin_func == JZ_FUNC_GPIO) {
		CSR_WRITE_4(sc, value ? JZ_GPIO_PAT0S : JZ_GPIO_PAT0C, mask);
		retval = 0;
	}
	JZ4780_GPIO_UNLOCK(sc);

	return (retval);
}

static int
jz4780_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val)
{
	struct jz4780_gpio_softc *sc;
	uint32_t data, mask;

	if (pin >= JZ4780_GPIO_PINS)
		return (EINVAL);

	mask = (1u << pin);
	sc = device_get_softc(dev);
	JZ4780_GPIO_LOCK(sc);
	data = CSR_READ_4(sc, JZ_GPIO_PIN);
	JZ4780_GPIO_UNLOCK(sc);
	*val = (data & mask) ? 1 : 0;

	return (0);
}

static int
jz4780_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct jz4780_gpio_softc *sc;
	uint32_t data, mask;
	int retval;

	if (pin >= JZ4780_GPIO_PINS)
		return (EINVAL);

	retval = EINVAL;
	mask = (1u << pin);
	sc = device_get_softc(dev);
	JZ4780_GPIO_LOCK(sc);
	if (sc->pins[pin].pin_func == JZ_FUNC_GPIO &&
	    sc->pins[pin].pin_flags & GPIO_PIN_OUTPUT) {
		data = CSR_READ_4(sc, JZ_GPIO_PIN);
		CSR_WRITE_4(sc, (data & mask) ? JZ_GPIO_PAT0C : JZ_GPIO_PAT0S,
		    mask);
		retval = 0;
	}
	JZ4780_GPIO_UNLOCK(sc);

	return (retval);
}

#ifdef FDT
static int
jz_gpio_map_intr_fdt(device_t dev, struct intr_map_data *data, u_int *irqp,
        enum intr_polarity *polp, enum intr_trigger *trigp)
{
	struct jz4780_gpio_softc *sc;
	struct intr_map_data_fdt *daf;

	sc = device_get_softc(dev);
	daf = (struct intr_map_data_fdt *)data;

	if (data == NULL || data->type != INTR_MAP_DATA_FDT ||
	    daf->ncells == 0 || daf->ncells > 2)
		return (EINVAL);

	*irqp = daf->cells[0];
	if (daf->ncells == 1) {
		*trigp = INTR_TRIGGER_CONFORM;
		*polp = INTR_POLARITY_CONFORM;
		return (0);
	}

	switch (daf->cells[1])
	{
	case IRQ_TYPE_EDGE_RISING:
		*trigp = INTR_TRIGGER_EDGE;
		*polp = INTR_POLARITY_HIGH;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		*trigp = INTR_TRIGGER_EDGE;
		*polp = INTR_POLARITY_LOW;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		*trigp = INTR_TRIGGER_LEVEL;
		*polp = INTR_POLARITY_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		*trigp = INTR_TRIGGER_LEVEL;
		*polp = INTR_POLARITY_LOW;
		break;
	default:
		device_printf(sc->dev, "unsupported trigger/polarity 0x%2x\n",
		    daf->cells[1]);
		return (ENOTSUP);
	}

	return (0);
}
#endif

static int
jz_gpio_map_intr(device_t dev, struct intr_map_data *data, u_int *irqp,
        enum intr_polarity *polp, enum intr_trigger *trigp)
{
	struct jz4780_gpio_softc *sc;
	enum intr_polarity pol;
	enum intr_trigger trig;
	u_int irq;

	sc = device_get_softc(dev);
	switch (data->type) {
#ifdef FDT
	case INTR_MAP_DATA_FDT:
		if (jz_gpio_map_intr_fdt(dev, data, &irq, &pol, &trig) != 0)
			return (EINVAL);
		break;
#endif
	default:
		return (EINVAL);
	}

	if (irq >= nitems(sc->pins))
		return (EINVAL);

	*irqp = irq;
	if (polp != NULL)
		*polp = pol;
	if (trigp != NULL)
		*trigp = trig;
	return (0);
}

static int
jz4780_gpio_pic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct jz4780_gpio_softc *sc;
	int retval;
	u_int irq;

	retval = jz_gpio_map_intr(dev, data, &irq, NULL, NULL);
	if (retval == 0) {
		sc = device_get_softc(dev);
		*isrcp = &sc->pins[irq].pin_irqsrc;
	}
	return (retval);
}

static int
jz4780_gpio_pic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
        struct resource *res, struct intr_map_data *data)
{
	struct jz4780_gpio_softc *sc;
	struct jz4780_gpio_pin *pin;
	enum intr_polarity pol;
	enum intr_trigger trig;
	uint32_t mask, irq;

	if (data == NULL)
		return (ENOTSUP);

	/* Get config for resource. */
	if (jz_gpio_map_intr(dev, data, &irq, &pol, &trig))
		return (EINVAL);

	pin = __containerof(isrc, struct jz4780_gpio_pin, pin_irqsrc);
	if (isrc != &pin->pin_irqsrc)
		return (EINVAL);

	/* Compare config if this is not first setup. */
	if (isrc->isrc_handlers != 0) {
		if ((pol != INTR_POLARITY_CONFORM && pol != pin->intr_polarity) ||
		    (trig != INTR_TRIGGER_CONFORM && trig != pin->intr_trigger))
			return (EINVAL);
		else
			return (0);
	}

	if (pol == INTR_POLARITY_CONFORM)
		pol = INTR_POLARITY_LOW;	/* just pick some */
	if (trig == INTR_TRIGGER_CONFORM)
		trig = INTR_TRIGGER_EDGE;	/* just pick some */

	sc = device_get_softc(dev);
	mask = 1u << pin->pin_num;

	JZ4780_GPIO_LOCK(sc);
	CSR_WRITE_4(sc, JZ_GPIO_MASKS, mask);
	CSR_WRITE_4(sc, JZ_GPIO_INTS, mask);

	if (trig == INTR_TRIGGER_LEVEL)
		CSR_WRITE_4(sc, JZ_GPIO_PAT1C, mask);
	else
		CSR_WRITE_4(sc, JZ_GPIO_PAT1S, mask);

	if (pol == INTR_POLARITY_LOW)
		CSR_WRITE_4(sc, JZ_GPIO_PAT0C, mask);
	else
		CSR_WRITE_4(sc, JZ_GPIO_PAT0S, mask);

	pin->pin_func = JZ_FUNC_INTR;
	pin->intr_trigger = trig;
	pin->intr_polarity = pol;

	CSR_WRITE_4(sc, JZ_GPIO_FLAGC, mask);
	CSR_WRITE_4(sc, JZ_GPIO_MASKC, mask);
	JZ4780_GPIO_UNLOCK(sc);
	return (0);
}

static void
jz4780_gpio_pic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct jz4780_gpio_softc *sc;
	struct jz4780_gpio_pin *pin;

	sc = device_get_softc(dev);
	pin = __containerof(isrc, struct jz4780_gpio_pin, pin_irqsrc);

	CSR_WRITE_4(sc, JZ_GPIO_MASKC, 1u << pin->pin_num);
}

static void
jz4780_gpio_pic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct jz4780_gpio_softc *sc;
	struct jz4780_gpio_pin *pin;

	sc = device_get_softc(dev);
	pin = __containerof(isrc, struct jz4780_gpio_pin, pin_irqsrc);

	CSR_WRITE_4(sc, JZ_GPIO_MASKS, 1u << pin->pin_num);
}

static void
jz4780_gpio_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	jz4780_gpio_pic_disable_intr(dev, isrc);
}

static void
jz4780_gpio_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	jz4780_gpio_pic_enable_intr(dev, isrc);
}

static void
jz4780_gpio_pic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct jz4780_gpio_softc *sc;
	struct jz4780_gpio_pin *pin;

	sc = device_get_softc(dev);
	pin = __containerof(isrc, struct jz4780_gpio_pin, pin_irqsrc);

	CSR_WRITE_4(sc, JZ_GPIO_FLAGC, 1u << pin->pin_num);
}

static int
jz4780_gpio_intr(void *arg)
{
	struct jz4780_gpio_softc *sc;
	uint32_t i, interrupts;

	sc = arg;
	interrupts = CSR_READ_4(sc, JZ_GPIO_FLAG);

	for (i = 0; interrupts != 0; i++, interrupts >>= 1) {
		if ((interrupts & 0x1) == 0)
			continue;
		if (intr_isrc_dispatch(&sc->pins[i].pin_irqsrc,
		    curthread->td_intr_frame) != 0) {
			device_printf(sc->dev, "spurious interrupt %d\n", i);
			PIC_DISABLE_INTR(sc->dev, &sc->pins[i].pin_irqsrc);
		}
	}

	return (FILTER_HANDLED);
}

static phandle_t
jz4780_gpio_bus_get_node(device_t bus, device_t dev)
{

	return (ofw_bus_get_node(bus));
}

static device_method_t jz4780_gpio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		jz4780_gpio_probe),
	DEVMETHOD(device_attach,	jz4780_gpio_attach),
	DEVMETHOD(device_detach,	jz4780_gpio_detach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus,		jz4780_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		jz4780_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	jz4780_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags,	jz4780_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps,	jz4780_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags,	jz4780_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get,		jz4780_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,		jz4780_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,	jz4780_gpio_pin_toggle),

	/* Custom interface to set pin function */
	DEVMETHOD(jz4780_gpio_configure_pin, jz4780_gpio_configure_pin),

	/* Interrupt controller interface */
	DEVMETHOD(pic_setup_intr,	jz4780_gpio_pic_setup_intr),
	DEVMETHOD(pic_enable_intr,	jz4780_gpio_pic_enable_intr),
	DEVMETHOD(pic_disable_intr,	jz4780_gpio_pic_disable_intr),
	DEVMETHOD(pic_map_intr,		jz4780_gpio_pic_map_intr),
	DEVMETHOD(pic_post_filter,	jz4780_gpio_pic_post_filter),
	DEVMETHOD(pic_post_ithread,	jz4780_gpio_pic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	jz4780_gpio_pic_pre_ithread),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	jz4780_gpio_bus_get_node),

	DEVMETHOD_END
};

static driver_t jz4780_gpio_driver = {
	"gpio",
	jz4780_gpio_methods,
	sizeof(struct jz4780_gpio_softc),
};

static devclass_t jz4780_gpio_devclass;

EARLY_DRIVER_MODULE(jz4780_gpio, simplebus, jz4780_gpio_driver,
    jz4780_gpio_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
