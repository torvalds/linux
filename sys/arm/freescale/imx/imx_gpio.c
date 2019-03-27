/*-
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Freescale i.MX515 GPIO driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/gpio.h>
#include <sys/proc.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/resource.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "gpio_if.h"

#ifdef INTRNG
#include "pic_if.h"
#endif

#define	WRITE4(_sc, _r, _v)						\
	    bus_space_write_4((_sc)->sc_iot, (_sc)->sc_ioh, (_r), (_v))
#define	READ4(_sc, _r)							\
	    bus_space_read_4((_sc)->sc_iot, (_sc)->sc_ioh, (_r))
#define	SET4(_sc, _r, _m)						\
	    WRITE4((_sc), (_r), READ4((_sc), (_r)) | (_m))
#define	CLEAR4(_sc, _r, _m)						\
	    WRITE4((_sc), (_r), READ4((_sc), (_r)) & ~(_m))

/* Registers definition for Freescale i.MX515 GPIO controller */

#define	IMX_GPIO_DR_REG		0x000 /* Pin Data */
#define	IMX_GPIO_OE_REG		0x004 /* Set Pin Output */
#define	IMX_GPIO_PSR_REG	0x008 /* Pad Status */
#define	IMX_GPIO_ICR1_REG	0x00C /* Interrupt Configuration */
#define	IMX_GPIO_ICR2_REG	0x010 /* Interrupt Configuration */
#define		GPIO_ICR_COND_LOW	0
#define		GPIO_ICR_COND_HIGH	1
#define		GPIO_ICR_COND_RISE	2
#define		GPIO_ICR_COND_FALL	3
#define		GPIO_ICR_COND_MASK	0x3
#define	IMX_GPIO_IMR_REG	0x014 /* Interrupt Mask Register */
#define	IMX_GPIO_ISR_REG	0x018 /* Interrupt Status Register */
#define	IMX_GPIO_EDGE_REG	0x01C /* Edge Detect Register */

#ifdef INTRNG
#define	DEFAULT_CAPS	(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | \
    GPIO_INTR_LEVEL_LOW | GPIO_INTR_LEVEL_HIGH | GPIO_INTR_EDGE_RISING | \
    GPIO_INTR_EDGE_FALLING | GPIO_INTR_EDGE_BOTH)
#else
#define	DEFAULT_CAPS	(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)
#endif

#define	NGPIO		32

#ifdef INTRNG
struct gpio_irqsrc {
	struct intr_irqsrc	gi_isrc;
	u_int			gi_irq;
	uint32_t		gi_mode;
};
#endif

struct imx51_gpio_softc {
	device_t		dev;
	device_t		sc_busdev;
	struct mtx		sc_mtx;
	struct resource		*sc_res[3]; /* 1 x mem, 2 x IRQ */
	void			*gpio_ih[2];
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			gpio_npins;
	struct gpio_pin		gpio_pins[NGPIO];
#ifdef INTRNG
	struct gpio_irqsrc 	gpio_pic_irqsrc[NGPIO];
#endif
};

static struct ofw_compat_data compat_data[] = {
	{"fsl,imx6q-gpio",  1},
	{"fsl,imx53-gpio",  1},
	{"fsl,imx51-gpio",  1},
	{NULL,	            0}
};

static struct resource_spec imx_gpio_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		1,	RF_ACTIVE },
	{ -1, 0 }
};
#define	FIRST_IRQRES	1
#define	NUM_IRQRES	2

/*
 * Helpers
 */
static void imx51_gpio_pin_configure(struct imx51_gpio_softc *,
    struct gpio_pin *, uint32_t);

/*
 * Driver stuff
 */
static int imx51_gpio_probe(device_t);
static int imx51_gpio_attach(device_t);
static int imx51_gpio_detach(device_t);

/*
 * GPIO interface
 */
static device_t imx51_gpio_get_bus(device_t);
static int imx51_gpio_pin_max(device_t, int *);
static int imx51_gpio_pin_getcaps(device_t, uint32_t, uint32_t *);
static int imx51_gpio_pin_getflags(device_t, uint32_t, uint32_t *);
static int imx51_gpio_pin_getname(device_t, uint32_t, char *);
static int imx51_gpio_pin_setflags(device_t, uint32_t, uint32_t);
static int imx51_gpio_pin_set(device_t, uint32_t, unsigned int);
static int imx51_gpio_pin_get(device_t, uint32_t, unsigned int *);
static int imx51_gpio_pin_toggle(device_t, uint32_t pin);

#ifdef INTRNG
static int
gpio_pic_map_fdt(struct imx51_gpio_softc *sc, struct intr_map_data_fdt *daf,
    u_int *irqp, uint32_t *modep)
{
	u_int irq;
	uint32_t mode;

	/*
	 * From devicetree/bindings/gpio/fsl-imx-gpio.txt:
	 *  #interrupt-cells:  2. The first cell is the GPIO number. The second
	 *  cell bits[3:0] is used to specify trigger type and level flags:
	 *    1 = low-to-high edge triggered.
	 *    2 = high-to-low edge triggered.
	 *    4 = active high level-sensitive.
	 *    8 = active low level-sensitive.
	 * We can do any single one of these modes, and also edge low+high
	 * (i.e., trigger on both edges); other combinations are not supported.
	 */

	if (daf->ncells != 2) {
		device_printf(sc->dev, "Invalid #interrupt-cells\n");
		return (EINVAL);
	}

	irq = daf->cells[0];
	if (irq >= sc->gpio_npins) {
		device_printf(sc->dev, "Invalid interrupt number %u\n", irq);
		return (EINVAL);
	}
	switch (daf->cells[1]) {
	case 1:
		mode = GPIO_INTR_EDGE_RISING;
		break;
	case 2:
		mode = GPIO_INTR_EDGE_FALLING;
		break;
	case 3:
		mode = GPIO_INTR_EDGE_BOTH;
		break;
	case 4:
		mode = GPIO_INTR_LEVEL_HIGH;
		break;
	case 8:
		mode = GPIO_INTR_LEVEL_LOW;
		break;
	default:
		device_printf(sc->dev, "Unsupported interrupt mode 0x%2x\n",
		    daf->cells[1]);
		return (ENOTSUP);
	}
	*irqp = irq;
	if (modep != NULL)
		*modep = mode;
	return (0);
}

static int
gpio_pic_map_gpio(struct imx51_gpio_softc *sc, struct intr_map_data_gpio *dag,
    u_int *irqp, uint32_t *modep)
{
	u_int irq;

	irq = dag->gpio_pin_num;
	if (irq >= sc->gpio_npins) {
		device_printf(sc->dev, "Invalid interrupt number %u\n", irq);
		return (EINVAL);
	}

	switch (dag->gpio_intr_mode) {
	case GPIO_INTR_LEVEL_LOW:
	case GPIO_INTR_LEVEL_HIGH:
	case GPIO_INTR_EDGE_RISING:
	case GPIO_INTR_EDGE_FALLING:
	case GPIO_INTR_EDGE_BOTH:
		break;
	default:
		device_printf(sc->dev, "Unsupported interrupt mode 0x%8x\n",
		    dag->gpio_intr_mode);
		return (EINVAL);
	}

	*irqp = irq;
	if (modep != NULL)
		*modep = dag->gpio_intr_mode;
	return (0);
}

static int
gpio_pic_map(struct imx51_gpio_softc *sc, struct intr_map_data *data,
    u_int *irqp, uint32_t *modep)
{

	switch (data->type) {
	case INTR_MAP_DATA_FDT:
		return (gpio_pic_map_fdt(sc, (struct intr_map_data_fdt *)data,
		    irqp, modep));
	case INTR_MAP_DATA_GPIO:
		return (gpio_pic_map_gpio(sc, (struct intr_map_data_gpio *)data,
		    irqp, modep));
	default:
		return (ENOTSUP);
	}
}

static int
gpio_pic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	int error;
	u_int irq;
	struct imx51_gpio_softc *sc;

	sc = device_get_softc(dev);
	error = gpio_pic_map(sc, data, &irq, NULL);
	if (error == 0)
		*isrcp = &sc->gpio_pic_irqsrc[irq].gi_isrc;
	return (error);
}

static int
gpio_pic_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct imx51_gpio_softc *sc;
	struct gpio_irqsrc *gi;

	sc = device_get_softc(dev);
	if (isrc->isrc_handlers == 0) {
		gi = (struct gpio_irqsrc *)isrc;
		gi->gi_mode = GPIO_INTR_CONFORM;

		// XXX Not sure this is necessary
		mtx_lock_spin(&sc->sc_mtx);
		CLEAR4(sc, IMX_GPIO_IMR_REG, (1U << gi->gi_irq));
		WRITE4(sc, IMX_GPIO_ISR_REG, (1U << gi->gi_irq));
		mtx_unlock_spin(&sc->sc_mtx);
	}
	return (0);
}

static int
gpio_pic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct imx51_gpio_softc *sc;
	struct gpio_irqsrc *gi;
	int error;
	u_int icfg, irq, reg, shift, wrk;
	uint32_t mode;

	if (data == NULL)
		return (ENOTSUP);

	sc = device_get_softc(dev);
	gi = (struct gpio_irqsrc *)isrc;

	/* Get config for interrupt. */
	error = gpio_pic_map(sc, data, &irq, &mode);
	if (error != 0)
		return (error);
	if (gi->gi_irq != irq)
		return (EINVAL);

	/* Compare config if this is not first setup. */
	if (isrc->isrc_handlers != 0)
		return (gi->gi_mode == mode ? 0 : EINVAL);
	gi->gi_mode = mode;

	/*
	 * To interrupt on both edges we have to use the EDGE register.  The
	 * manual says it only exists for backwards compatibilty with older imx
	 * chips, but it's also the only way to configure interrupting on both
	 * edges.  If the EDGE bit is on, the corresponding ICRn bit is ignored.
	 */
	mtx_lock_spin(&sc->sc_mtx);
	if (mode == GPIO_INTR_EDGE_BOTH) {
		SET4(sc, IMX_GPIO_EDGE_REG, (1u << irq));
	} else {
		CLEAR4(sc, IMX_GPIO_EDGE_REG, (1u << irq));
		switch (mode) {
		default: 
			/* silence warnings; default can't actually happen. */
			/* FALLTHROUGH */
		case GPIO_INTR_LEVEL_LOW:
			icfg = GPIO_ICR_COND_LOW;
			break;
		case GPIO_INTR_LEVEL_HIGH:
			icfg = GPIO_ICR_COND_HIGH;
			break;
		case GPIO_INTR_EDGE_RISING:
			icfg = GPIO_ICR_COND_RISE;
			break;
		case GPIO_INTR_EDGE_FALLING:
			icfg = GPIO_ICR_COND_FALL;
			break;
		}
		if (irq < 16) {
			reg = IMX_GPIO_ICR1_REG;
			shift = 2 * irq;
		} else {
			reg = IMX_GPIO_ICR2_REG;
			shift = 2 * (irq - 16);
		}
		wrk = READ4(sc, reg);
		wrk &= ~(GPIO_ICR_COND_MASK << shift);
		wrk |= icfg << shift;
		WRITE4(sc, reg, wrk);
	}
	WRITE4(sc, IMX_GPIO_ISR_REG, (1u << irq));
	SET4(sc, IMX_GPIO_IMR_REG, (1u << irq));
	mtx_unlock_spin(&sc->sc_mtx);

	return (0);
}

/*
 * this is mask_intr
 */
static void
gpio_pic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct imx51_gpio_softc *sc;
	u_int irq;

	sc = device_get_softc(dev);
	irq = ((struct gpio_irqsrc *)isrc)->gi_irq;

	mtx_lock_spin(&sc->sc_mtx);
	CLEAR4(sc, IMX_GPIO_IMR_REG, (1U << irq));
	mtx_unlock_spin(&sc->sc_mtx);
}

/*
 * this is unmask_intr
 */
static void
gpio_pic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct imx51_gpio_softc *sc;
	u_int irq;

	sc = device_get_softc(dev);
	irq = ((struct gpio_irqsrc *)isrc)->gi_irq;

	mtx_lock_spin(&sc->sc_mtx);
	SET4(sc, IMX_GPIO_IMR_REG, (1U << irq));
	mtx_unlock_spin(&sc->sc_mtx);
}

static void
gpio_pic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct imx51_gpio_softc *sc;
	u_int irq;

	sc = device_get_softc(dev);
	irq = ((struct gpio_irqsrc *)isrc)->gi_irq;

	arm_irq_memory_barrier(0);
        /* EOI.  W1C reg so no r-m-w, no locking needed. */
	WRITE4(sc, IMX_GPIO_ISR_REG, (1U << irq));
}

static void
gpio_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct imx51_gpio_softc *sc;
	u_int irq;

	sc = device_get_softc(dev);
	irq = ((struct gpio_irqsrc *)isrc)->gi_irq;

	arm_irq_memory_barrier(0);
	/* EOI.  W1C reg so no r-m-w, no locking needed. */
	WRITE4(sc, IMX_GPIO_ISR_REG, (1U << irq));
	gpio_pic_enable_intr(dev, isrc);
}

static void
gpio_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	gpio_pic_disable_intr(dev, isrc);
}

static int
gpio_pic_filter(void *arg)
{
	struct imx51_gpio_softc *sc;
	struct intr_irqsrc *isrc;
	uint32_t i, interrupts;

	sc = arg;
	mtx_lock_spin(&sc->sc_mtx);
	interrupts = READ4(sc, IMX_GPIO_ISR_REG) & READ4(sc, IMX_GPIO_IMR_REG);
	mtx_unlock_spin(&sc->sc_mtx);

	for (i = 0; interrupts != 0; i++, interrupts >>= 1) {
		if ((interrupts & 0x1) == 0)
			continue;
		isrc = &sc->gpio_pic_irqsrc[i].gi_isrc;
		if (intr_isrc_dispatch(isrc, curthread->td_intr_frame) != 0) {
			gpio_pic_disable_intr(sc->dev, isrc);
			gpio_pic_post_filter(sc->dev, isrc);
			device_printf(sc->dev, "Stray irq %u disabled\n", i);
		}
	}

	return (FILTER_HANDLED);
}

/*
 * Initialize our isrcs and register them with intrng.
 */
static int
gpio_pic_register_isrcs(struct imx51_gpio_softc *sc)
{
	int error;
	uint32_t irq;
	const char *name;

	name = device_get_nameunit(sc->dev);
	for (irq = 0; irq < NGPIO; irq++) {
		sc->gpio_pic_irqsrc[irq].gi_irq = irq;
		sc->gpio_pic_irqsrc[irq].gi_mode = GPIO_INTR_CONFORM;

		error = intr_isrc_register(&sc->gpio_pic_irqsrc[irq].gi_isrc,
		    sc->dev, 0, "%s,%u", name, irq);
		if (error != 0) {
			/* XXX call intr_isrc_deregister() */
			device_printf(sc->dev, "%s failed", __func__);
			return (error);
		}
	}
	return (0);
}
#endif

/*
 *
 */
static void
imx51_gpio_pin_configure(struct imx51_gpio_softc *sc, struct gpio_pin *pin,
    unsigned int flags)
{
	u_int newflags, pad;

	mtx_lock_spin(&sc->sc_mtx);

	/*
	 * Manage input/output; other flags not supported yet (maybe not ever,
	 * since we have no connection to the pad config registers from here).
	 *
	 * When setting a pin to output, honor the PRESET_[LOW,HIGH] flags if
	 * present.  Otherwise, for glitchless transistions on pins with pulls,
	 * read the current state of the pad and preset the DR register to drive
	 * the current value onto the pin before enabling the pin for output.
	 *
	 * Note that changes to pin->gp_flags must be acccumulated in newflags
	 * and stored with a single writeback to gp_flags at the end, to enable
	 * unlocked reads of that value elsewhere. This is only about unlocked
	 * access to gp_flags from elsewhere; we still use locking in this
	 * function to protect r-m-w access to the hardware registers.
	 */
	if (flags & (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)) {
		newflags = pin->gp_flags & ~(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT);
		if (flags & GPIO_PIN_OUTPUT) {
			if (flags & GPIO_PIN_PRESET_LOW) {
				pad = 0;
			} else if (flags & GPIO_PIN_PRESET_HIGH) {
				pad = 1;
			} else {
				if (flags & GPIO_PIN_OPENDRAIN)
					pad = READ4(sc, IMX_GPIO_PSR_REG);
				else
					pad = READ4(sc, IMX_GPIO_DR_REG);
				pad = (pad >> pin->gp_pin) & 1;
			}
			newflags |= GPIO_PIN_OUTPUT;
			SET4(sc, IMX_GPIO_DR_REG, (pad << pin->gp_pin));
			SET4(sc, IMX_GPIO_OE_REG, (1U << pin->gp_pin));
		} else {
			newflags |= GPIO_PIN_INPUT;
			CLEAR4(sc, IMX_GPIO_OE_REG, (1U << pin->gp_pin));
		}
		pin->gp_flags = newflags;
	}

	mtx_unlock_spin(&sc->sc_mtx);
}

static device_t
imx51_gpio_get_bus(device_t dev)
{
	struct imx51_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->sc_busdev);
}

static int
imx51_gpio_pin_max(device_t dev, int *maxpin)
{
	struct imx51_gpio_softc *sc;

	sc = device_get_softc(dev);
	*maxpin = sc->gpio_npins - 1;

	return (0);
}

static int
imx51_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct imx51_gpio_softc *sc;

	sc = device_get_softc(dev);

	if (pin >= sc->gpio_npins)
		return (EINVAL);

	*caps = sc->gpio_pins[pin].gp_caps;

	return (0);
}

static int
imx51_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct imx51_gpio_softc *sc;

	sc = device_get_softc(dev);

	if (pin >= sc->gpio_npins)
		return (EINVAL);

	*flags = sc->gpio_pins[pin].gp_flags;

	return (0);
}

static int
imx51_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct imx51_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	mtx_lock_spin(&sc->sc_mtx);
	memcpy(name, sc->gpio_pins[pin].gp_name, GPIOMAXNAME);
	mtx_unlock_spin(&sc->sc_mtx);

	return (0);
}

static int
imx51_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct imx51_gpio_softc *sc;

	sc = device_get_softc(dev);

	if (pin >= sc->gpio_npins)
		return (EINVAL);

	imx51_gpio_pin_configure(sc, &sc->gpio_pins[pin], flags);

	return (0);
}

static int
imx51_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct imx51_gpio_softc *sc;

	sc = device_get_softc(dev);

	if (pin >= sc->gpio_npins)
		return (EINVAL);

	mtx_lock_spin(&sc->sc_mtx);
	if (value)
		SET4(sc, IMX_GPIO_DR_REG, (1U << pin));
	else
		CLEAR4(sc, IMX_GPIO_DR_REG, (1U << pin));
	mtx_unlock_spin(&sc->sc_mtx);

	return (0);
}

static int
imx51_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val)
{
	struct imx51_gpio_softc *sc;

	sc = device_get_softc(dev);

	if (pin >= sc->gpio_npins)
		return (EINVAL);

	/*
	 * Normally a pin set for output can be read by reading the DR reg which
	 * indicates what value is being driven to that pin.  The exception is
	 * pins configured for open-drain mode, in which case we have to read
	 * the pad status register in case the pin is being driven externally.
	 * Doing so requires that the SION bit be configured in pinmux, which
	 * isn't the case for most normal gpio pins, so only try to read via PSR
	 * if the OPENDRAIN flag is set, and it's the user's job to correctly
	 * configure SION along with open-drain output mode for those pins.
	 */
	if (sc->gpio_pins[pin].gp_flags & GPIO_PIN_OPENDRAIN)
		*val = (READ4(sc, IMX_GPIO_PSR_REG) >> pin) & 1;
	else
		*val = (READ4(sc, IMX_GPIO_DR_REG) >> pin) & 1;

	return (0);
}

static int
imx51_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct imx51_gpio_softc *sc;

	sc = device_get_softc(dev);

	if (pin >= sc->gpio_npins)
		return (EINVAL);

	mtx_lock_spin(&sc->sc_mtx);
	WRITE4(sc, IMX_GPIO_DR_REG,
	    (READ4(sc, IMX_GPIO_DR_REG) ^ (1U << pin)));
	mtx_unlock_spin(&sc->sc_mtx);

	return (0);
}

static int
imx51_gpio_pin_access_32(device_t dev, uint32_t first_pin, uint32_t clear_pins,
    uint32_t change_pins, uint32_t *orig_pins)
{
	struct imx51_gpio_softc *sc;

	if (first_pin != 0)
		return (EINVAL);

	sc = device_get_softc(dev);

	if (orig_pins != NULL)
		*orig_pins = READ4(sc, IMX_GPIO_DR_REG);

	if ((clear_pins | change_pins) != 0) {
		mtx_lock_spin(&sc->sc_mtx);
		WRITE4(sc, IMX_GPIO_DR_REG,
		    (READ4(sc, IMX_GPIO_DR_REG) & ~clear_pins) ^ change_pins);
		mtx_unlock_spin(&sc->sc_mtx);
	}

	return (0);
}

static int
imx51_gpio_pin_config_32(device_t dev, uint32_t first_pin, uint32_t num_pins,
    uint32_t *pin_flags)
{
	struct imx51_gpio_softc *sc;
	u_int i;
	uint32_t bit, drclr, drset, flags, oeclr, oeset, pads;

	sc = device_get_softc(dev);

	if (first_pin != 0 || num_pins > sc->gpio_npins)
		return (EINVAL);

	drclr = drset = oeclr = oeset = 0;
	pads = READ4(sc, IMX_GPIO_DR_REG);

	for (i = 0; i < num_pins; ++i) {
		bit = 1u << i;
		flags = pin_flags[i];
		if (flags & GPIO_PIN_INPUT) {
			oeclr |= bit;
		} else if (flags & GPIO_PIN_OUTPUT) {
			oeset |= bit;
			if (flags & GPIO_PIN_PRESET_LOW)
				drclr |= bit;
			else if (flags & GPIO_PIN_PRESET_HIGH)
				drset |= bit;
			else /* Drive whatever it's now pulled to. */
				drset |= pads & bit;
		}
	}

	mtx_lock_spin(&sc->sc_mtx);
	WRITE4(sc, IMX_GPIO_DR_REG,
	    (READ4(sc, IMX_GPIO_DR_REG) & ~drclr) | drset);
	WRITE4(sc, IMX_GPIO_OE_REG,
	    (READ4(sc, IMX_GPIO_OE_REG) & ~oeclr) | oeset);
	mtx_unlock_spin(&sc->sc_mtx);

	return (0);
}

static int
imx51_gpio_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "Freescale i.MX GPIO Controller");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
imx51_gpio_attach(device_t dev)
{
	struct imx51_gpio_softc *sc;
	int i, irq, unit;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->gpio_npins = NGPIO;

	mtx_init(&sc->sc_mtx, device_get_nameunit(sc->dev), NULL, MTX_SPIN);

	if (bus_alloc_resources(dev, imx_gpio_spec, sc->sc_res)) {
		device_printf(dev, "could not allocate resources\n");
		bus_release_resources(dev, imx_gpio_spec, sc->sc_res);
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}

	sc->sc_iot = rman_get_bustag(sc->sc_res[0]);
	sc->sc_ioh = rman_get_bushandle(sc->sc_res[0]);
	/*
	 * Mask off all interrupts in hardware, then set up interrupt handling.
	 */
	WRITE4(sc, IMX_GPIO_IMR_REG, 0);
	for (irq = 0; irq < 2; irq++) {
#ifdef INTRNG
		if ((bus_setup_intr(dev, sc->sc_res[1 + irq], INTR_TYPE_CLK,
		    gpio_pic_filter, NULL, sc, &sc->gpio_ih[irq]))) {
			device_printf(dev,
			    "WARNING: unable to register interrupt handler\n");
			imx51_gpio_detach(dev);
			return (ENXIO);
		}
#endif		
	}

	unit = device_get_unit(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
 		sc->gpio_pins[i].gp_pin = i;
 		sc->gpio_pins[i].gp_caps = DEFAULT_CAPS;
 		sc->gpio_pins[i].gp_flags =
 		    (READ4(sc, IMX_GPIO_OE_REG) & (1U << i)) ? GPIO_PIN_OUTPUT :
 		    GPIO_PIN_INPUT;
 		snprintf(sc->gpio_pins[i].gp_name, GPIOMAXNAME,
 		    "GPIO%d_IO%02d", unit + 1, i);
	}

#ifdef INTRNG
	gpio_pic_register_isrcs(sc);
	intr_pic_register(dev, OF_xref_from_node(ofw_bus_get_node(dev)));
#endif
	sc->sc_busdev = gpiobus_attach_bus(dev);
	
	if (sc->sc_busdev == NULL) {
		imx51_gpio_detach(dev);
		return (ENXIO);
	}

	return (0);
}

static int
imx51_gpio_detach(device_t dev)
{
	int irq;
	struct imx51_gpio_softc *sc;

	sc = device_get_softc(dev);

	gpiobus_detach_bus(dev);
	for (irq = 0; irq < NUM_IRQRES; irq++) {
		if (sc->gpio_ih[irq])
			bus_teardown_intr(dev, sc->sc_res[irq + FIRST_IRQRES],
			    sc->gpio_ih[irq]);
	}
	bus_release_resources(dev, imx_gpio_spec, sc->sc_res);
	mtx_destroy(&sc->sc_mtx);

	return(0);
}

static device_method_t imx51_gpio_methods[] = {
	DEVMETHOD(device_probe,		imx51_gpio_probe),
	DEVMETHOD(device_attach,	imx51_gpio_attach),
	DEVMETHOD(device_detach,	imx51_gpio_detach),

#ifdef INTRNG
	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	gpio_pic_disable_intr),
	DEVMETHOD(pic_enable_intr,	gpio_pic_enable_intr),
	DEVMETHOD(pic_map_intr,		gpio_pic_map_intr),
	DEVMETHOD(pic_setup_intr,	gpio_pic_setup_intr),
	DEVMETHOD(pic_teardown_intr,	gpio_pic_teardown_intr),
	DEVMETHOD(pic_post_filter,	gpio_pic_post_filter),
	DEVMETHOD(pic_post_ithread,	gpio_pic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	gpio_pic_pre_ithread),
#endif

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus,		imx51_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		imx51_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	imx51_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags,	imx51_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps,	imx51_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags,	imx51_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get,		imx51_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,		imx51_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,	imx51_gpio_pin_toggle),
	DEVMETHOD(gpio_pin_access_32,	imx51_gpio_pin_access_32),
	DEVMETHOD(gpio_pin_config_32,	imx51_gpio_pin_config_32),
	{0, 0},
};

static driver_t imx51_gpio_driver = {
	"gpio",
	imx51_gpio_methods,
	sizeof(struct imx51_gpio_softc),
};
static devclass_t imx51_gpio_devclass;

EARLY_DRIVER_MODULE(imx51_gpio, simplebus, imx51_gpio_driver,
    imx51_gpio_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
