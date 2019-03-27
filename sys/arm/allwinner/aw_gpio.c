/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ganbold Tsagaankhuu <ganbold@freebsd.org>
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * Copyright (c) 2012 Luiz Otavio O Souza.
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
#include <dev/fdt/fdt_pinctrl.h>

#include <arm/allwinner/aw_machdep.h>
#include <arm/allwinner/allwinner_pinctrl.h>
#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>

#if defined(__aarch64__)
#include "opt_soc.h"
#endif

#include "gpio_if.h"

#define	AW_GPIO_DEFAULT_CAPS	(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT |	\
    GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN)

#define	AW_GPIO_NONE		0
#define	AW_GPIO_PULLUP		1
#define	AW_GPIO_PULLDOWN	2

#define	AW_GPIO_INPUT		0
#define	AW_GPIO_OUTPUT		1

#define	AW_GPIO_DRV_MASK	0x3
#define	AW_GPIO_PUD_MASK	0x3

#define	AW_PINCTRL	1
#define	AW_R_PINCTRL	2

/* Defined in aw_padconf.c */
#ifdef SOC_ALLWINNER_A10
extern const struct allwinner_padconf a10_padconf;
#endif

/* Defined in a13_padconf.c */
#ifdef SOC_ALLWINNER_A13
extern const struct allwinner_padconf a13_padconf;
#endif

/* Defined in a20_padconf.c */
#ifdef SOC_ALLWINNER_A20
extern const struct allwinner_padconf a20_padconf;
#endif

/* Defined in a31_padconf.c */
#ifdef SOC_ALLWINNER_A31
extern const struct allwinner_padconf a31_padconf;
#endif

/* Defined in a31s_padconf.c */
#ifdef SOC_ALLWINNER_A31S
extern const struct allwinner_padconf a31s_padconf;
#endif

#if defined(SOC_ALLWINNER_A31) || defined(SOC_ALLWINNER_A31S)
extern const struct allwinner_padconf a31_r_padconf;
#endif

/* Defined in a33_padconf.c */
#ifdef SOC_ALLWINNER_A33
extern const struct allwinner_padconf a33_padconf;
#endif

/* Defined in h3_padconf.c */
#if defined(SOC_ALLWINNER_H3) || defined(SOC_ALLWINNER_H5)
extern const struct allwinner_padconf h3_padconf;
extern const struct allwinner_padconf h3_r_padconf;
#endif

/* Defined in a83t_padconf.c */
#ifdef SOC_ALLWINNER_A83T
extern const struct allwinner_padconf a83t_padconf;
extern const struct allwinner_padconf a83t_r_padconf;
#endif

/* Defined in a64_padconf.c */
#ifdef SOC_ALLWINNER_A64
extern const struct allwinner_padconf a64_padconf;
extern const struct allwinner_padconf a64_r_padconf;
#endif

static struct ofw_compat_data compat_data[] = {
#ifdef SOC_ALLWINNER_A10
	{"allwinner,sun4i-a10-pinctrl",		(uintptr_t)&a10_padconf},
#endif
#ifdef SOC_ALLWINNER_A13
	{"allwinner,sun5i-a13-pinctrl",		(uintptr_t)&a13_padconf},
#endif
#ifdef SOC_ALLWINNER_A20
	{"allwinner,sun7i-a20-pinctrl",		(uintptr_t)&a20_padconf},
#endif
#ifdef SOC_ALLWINNER_A31
	{"allwinner,sun6i-a31-pinctrl",		(uintptr_t)&a31_padconf},
#endif
#ifdef SOC_ALLWINNER_A31S
	{"allwinner,sun6i-a31s-pinctrl",	(uintptr_t)&a31s_padconf},
#endif
#if defined(SOC_ALLWINNER_A31) || defined(SOC_ALLWINNER_A31S)
	{"allwinner,sun6i-a31-r-pinctrl",	(uintptr_t)&a31_r_padconf},
#endif
#ifdef SOC_ALLWINNER_A33
	{"allwinner,sun6i-a33-pinctrl",		(uintptr_t)&a33_padconf},
#endif
#ifdef SOC_ALLWINNER_A83T
	{"allwinner,sun8i-a83t-pinctrl",	(uintptr_t)&a83t_padconf},
	{"allwinner,sun8i-a83t-r-pinctrl",	(uintptr_t)&a83t_r_padconf},
#endif
#if defined(SOC_ALLWINNER_H3) || defined(SOC_ALLWINNER_H5)
	{"allwinner,sun8i-h3-pinctrl",		(uintptr_t)&h3_padconf},
	{"allwinner,sun50i-h5-pinctrl",		(uintptr_t)&h3_padconf},
	{"allwinner,sun8i-h3-r-pinctrl",	(uintptr_t)&h3_r_padconf},
#endif
#ifdef SOC_ALLWINNER_A64
	{"allwinner,sun50i-a64-pinctrl",	(uintptr_t)&a64_padconf},
	{"allwinner,sun50i-a64-r-pinctrl",	(uintptr_t)&a64_r_padconf},
#endif
	{NULL,	0}
};

struct clk_list {
	TAILQ_ENTRY(clk_list)	next;
	clk_t			clk;
};

struct aw_gpio_softc {
	device_t		sc_dev;
	device_t		sc_busdev;
	struct mtx		sc_mtx;
	struct resource *	sc_mem_res;
	struct resource *	sc_irq_res;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	void *			sc_intrhand;
	const struct allwinner_padconf *	padconf;
	TAILQ_HEAD(, clk_list)		clk_list;
};

#define	AW_GPIO_LOCK(_sc)		mtx_lock_spin(&(_sc)->sc_mtx)
#define	AW_GPIO_UNLOCK(_sc)		mtx_unlock_spin(&(_sc)->sc_mtx)
#define	AW_GPIO_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)

#define	AW_GPIO_GP_CFG(_bank, _idx)	0x00 + ((_bank) * 0x24) + ((_idx) << 2)
#define	AW_GPIO_GP_DAT(_bank)		0x10 + ((_bank) * 0x24)
#define	AW_GPIO_GP_DRV(_bank, _idx)	0x14 + ((_bank) * 0x24) + ((_idx) << 2)
#define	AW_GPIO_GP_PUL(_bank, _idx)	0x1c + ((_bank) * 0x24) + ((_idx) << 2)

#define	AW_GPIO_GP_INT_CFG0		0x200
#define	AW_GPIO_GP_INT_CFG1		0x204
#define	AW_GPIO_GP_INT_CFG2		0x208
#define	AW_GPIO_GP_INT_CFG3		0x20c

#define	AW_GPIO_GP_INT_CTL		0x210
#define	AW_GPIO_GP_INT_STA		0x214
#define	AW_GPIO_GP_INT_DEB		0x218

static char *aw_gpio_parse_function(phandle_t node);
static const char **aw_gpio_parse_pins(phandle_t node, int *pins_nb);
static uint32_t aw_gpio_parse_bias(phandle_t node);
static int aw_gpio_parse_drive_strength(phandle_t node, uint32_t *drive);

static int aw_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *value);
static int aw_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value);
static int aw_gpio_pin_get_locked(struct aw_gpio_softc *sc, uint32_t pin, unsigned int *value);
static int aw_gpio_pin_set_locked(struct aw_gpio_softc *sc, uint32_t pin, unsigned int value);

#define	AW_GPIO_WRITE(_sc, _off, _val)		\
    bus_space_write_4(_sc->sc_bst, _sc->sc_bsh, _off, _val)
#define	AW_GPIO_READ(_sc, _off)		\
    bus_space_read_4(_sc->sc_bst, _sc->sc_bsh, _off)

static uint32_t
aw_gpio_get_function(struct aw_gpio_softc *sc, uint32_t pin)
{
	uint32_t bank, func, offset;

	/* Must be called with lock held. */
	AW_GPIO_LOCK_ASSERT(sc);

	if (pin > sc->padconf->npins)
		return (0);
	bank = sc->padconf->pins[pin].port;
	pin = sc->padconf->pins[pin].pin;
	offset = ((pin & 0x07) << 2);

	func = AW_GPIO_READ(sc, AW_GPIO_GP_CFG(bank, pin >> 3));

	return ((func >> offset) & 0x7);
}

static int
aw_gpio_set_function(struct aw_gpio_softc *sc, uint32_t pin, uint32_t f)
{
	uint32_t bank, data, offset;

	/* Check if the function exists in the padconf data */
	if (sc->padconf->pins[pin].functions[f] == NULL)
		return (EINVAL);

	/* Must be called with lock held. */
	AW_GPIO_LOCK_ASSERT(sc);

	bank = sc->padconf->pins[pin].port;
	pin = sc->padconf->pins[pin].pin;
	offset = ((pin & 0x07) << 2);

	data = AW_GPIO_READ(sc, AW_GPIO_GP_CFG(bank, pin >> 3));
	data &= ~(7 << offset);
	data |= (f << offset);
	AW_GPIO_WRITE(sc, AW_GPIO_GP_CFG(bank, pin >> 3), data);

	return (0);
}

static uint32_t
aw_gpio_get_pud(struct aw_gpio_softc *sc, uint32_t pin)
{
	uint32_t bank, offset, val;

	/* Must be called with lock held. */
	AW_GPIO_LOCK_ASSERT(sc);

	bank = sc->padconf->pins[pin].port;
	pin = sc->padconf->pins[pin].pin;
	offset = ((pin & 0x0f) << 1);

	val = AW_GPIO_READ(sc, AW_GPIO_GP_PUL(bank, pin >> 4));

	return ((val >> offset) & AW_GPIO_PUD_MASK);
}

static void
aw_gpio_set_pud(struct aw_gpio_softc *sc, uint32_t pin, uint32_t state)
{
	uint32_t bank, offset, val;

	if (aw_gpio_get_pud(sc, pin) == state)
		return;

	/* Must be called with lock held. */
	AW_GPIO_LOCK_ASSERT(sc);

	bank = sc->padconf->pins[pin].port;
	pin = sc->padconf->pins[pin].pin;
	offset = ((pin & 0x0f) << 1);

	val = AW_GPIO_READ(sc, AW_GPIO_GP_PUL(bank, pin >> 4));
	val &= ~(AW_GPIO_PUD_MASK << offset);
	val |= (state << offset);
	AW_GPIO_WRITE(sc, AW_GPIO_GP_PUL(bank, pin >> 4), val);
}

static uint32_t
aw_gpio_get_drv(struct aw_gpio_softc *sc, uint32_t pin)
{
	uint32_t bank, offset, val;

	/* Must be called with lock held. */
	AW_GPIO_LOCK_ASSERT(sc);

	bank = sc->padconf->pins[pin].port;
	pin = sc->padconf->pins[pin].pin;
	offset = ((pin & 0x0f) << 1);

	val = AW_GPIO_READ(sc, AW_GPIO_GP_DRV(bank, pin >> 4));

	return ((val >> offset) & AW_GPIO_DRV_MASK);
}

static void
aw_gpio_set_drv(struct aw_gpio_softc *sc, uint32_t pin, uint32_t drive)
{
	uint32_t bank, offset, val;

	if (aw_gpio_get_drv(sc, pin) == drive)
		return;

	/* Must be called with lock held. */
	AW_GPIO_LOCK_ASSERT(sc);

	bank = sc->padconf->pins[pin].port;
	pin = sc->padconf->pins[pin].pin;
	offset = ((pin & 0x0f) << 1);

	val = AW_GPIO_READ(sc, AW_GPIO_GP_DRV(bank, pin >> 4));
	val &= ~(AW_GPIO_DRV_MASK << offset);
	val |= (drive << offset);
	AW_GPIO_WRITE(sc, AW_GPIO_GP_DRV(bank, pin >> 4), val);
}

static int
aw_gpio_pin_configure(struct aw_gpio_softc *sc, uint32_t pin, uint32_t flags)
{
	u_int val;
	int err = 0;

	/* Must be called with lock held. */
	AW_GPIO_LOCK_ASSERT(sc);

	if (pin > sc->padconf->npins)
		return (EINVAL);

	/* Manage input/output. */
	if (flags & GPIO_PIN_INPUT) {
		err = aw_gpio_set_function(sc, pin, AW_GPIO_INPUT);
	} else if ((flags & GPIO_PIN_OUTPUT) &&
	    aw_gpio_get_function(sc, pin) != AW_GPIO_OUTPUT) {
		if (flags & GPIO_PIN_PRESET_LOW) {
			aw_gpio_pin_set_locked(sc, pin, 0);
		} else if (flags & GPIO_PIN_PRESET_HIGH) {
			aw_gpio_pin_set_locked(sc, pin, 1);
		} else {
			/* Read the pin and preset output to current state. */
			err = aw_gpio_set_function(sc, pin, AW_GPIO_INPUT);
			if (err == 0) {
				aw_gpio_pin_get_locked(sc, pin, &val);
				aw_gpio_pin_set_locked(sc, pin, val);
			}
		}
		if (err == 0)
			err = aw_gpio_set_function(sc, pin, AW_GPIO_OUTPUT);
	}

	if (err)
		return (err);

	/* Manage Pull-up/pull-down. */
	if (flags & GPIO_PIN_PULLUP)
		aw_gpio_set_pud(sc, pin, AW_GPIO_PULLUP);
	else if (flags & GPIO_PIN_PULLDOWN)
		aw_gpio_set_pud(sc, pin, AW_GPIO_PULLDOWN);
	else
		aw_gpio_set_pud(sc, pin, AW_GPIO_NONE);

	return (0);
}

static device_t
aw_gpio_get_bus(device_t dev)
{
	struct aw_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->sc_busdev);
}

static int
aw_gpio_pin_max(device_t dev, int *maxpin)
{
	struct aw_gpio_softc *sc;

	sc = device_get_softc(dev);

	*maxpin = sc->padconf->npins - 1;
	return (0);
}

static int
aw_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct aw_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->padconf->npins)
		return (EINVAL);

	*caps = AW_GPIO_DEFAULT_CAPS;

	return (0);
}

static int
aw_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct aw_gpio_softc *sc;
	uint32_t func;
	uint32_t pud;

	sc = device_get_softc(dev);
	if (pin >= sc->padconf->npins)
		return (EINVAL);

	AW_GPIO_LOCK(sc);
	func = aw_gpio_get_function(sc, pin);
	switch (func) {
	case AW_GPIO_INPUT:
		*flags = GPIO_PIN_INPUT;
		break;
	case AW_GPIO_OUTPUT:
		*flags = GPIO_PIN_OUTPUT;
		break;
	default:
		*flags = 0;
		break;
	}

	pud = aw_gpio_get_pud(sc, pin);
	switch (pud) {
	case AW_GPIO_PULLDOWN:
		*flags |= GPIO_PIN_PULLDOWN;
		break;
	case AW_GPIO_PULLUP:
		*flags |= GPIO_PIN_PULLUP;
		break;
	default:
		break;
	}

	AW_GPIO_UNLOCK(sc);

	return (0);
}

static int
aw_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct aw_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->padconf->npins)
		return (EINVAL);

	snprintf(name, GPIOMAXNAME - 1, "%s",
	    sc->padconf->pins[pin].name);
	name[GPIOMAXNAME - 1] = '\0';

	return (0);
}

static int
aw_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct aw_gpio_softc *sc;
	int err;

	sc = device_get_softc(dev);
	if (pin > sc->padconf->npins)
		return (EINVAL);

	AW_GPIO_LOCK(sc);
	err = aw_gpio_pin_configure(sc, pin, flags);
	AW_GPIO_UNLOCK(sc);

	return (err);
}

static int
aw_gpio_pin_set_locked(struct aw_gpio_softc *sc, uint32_t pin,
    unsigned int value)
{
	uint32_t bank, data;

	AW_GPIO_LOCK_ASSERT(sc);

	if (pin > sc->padconf->npins)
		return (EINVAL);

	bank = sc->padconf->pins[pin].port;
	pin = sc->padconf->pins[pin].pin;

	data = AW_GPIO_READ(sc, AW_GPIO_GP_DAT(bank));
	if (value)
		data |= (1 << pin);
	else
		data &= ~(1 << pin);
	AW_GPIO_WRITE(sc, AW_GPIO_GP_DAT(bank), data);

	return (0);
}

static int
aw_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct aw_gpio_softc *sc;
	int ret;

	sc = device_get_softc(dev);

	AW_GPIO_LOCK(sc);
	ret = aw_gpio_pin_set_locked(sc, pin, value);
	AW_GPIO_UNLOCK(sc);

	return (ret);
}

static int
aw_gpio_pin_get_locked(struct aw_gpio_softc *sc,uint32_t pin,
    unsigned int *val)
{
	uint32_t bank, reg_data;

	AW_GPIO_LOCK_ASSERT(sc);

	if (pin > sc->padconf->npins)
		return (EINVAL);

	bank = sc->padconf->pins[pin].port;
	pin = sc->padconf->pins[pin].pin;

	reg_data = AW_GPIO_READ(sc, AW_GPIO_GP_DAT(bank));
	*val = (reg_data & (1 << pin)) ? 1 : 0;

	return (0);
}

static char *
aw_gpio_parse_function(phandle_t node)
{
	char *function;

	if (OF_getprop_alloc(node, "function",
	    (void **)&function) != -1)
		return (function);
	if (OF_getprop_alloc(node, "allwinner,function",
	    (void **)&function) != -1)
		return (function);

	return (NULL);
}

static const char **
aw_gpio_parse_pins(phandle_t node, int *pins_nb)
{
	const char **pinlist;

	*pins_nb = ofw_bus_string_list_to_array(node, "pins", &pinlist);
	if (*pins_nb > 0)
		return (pinlist);

	*pins_nb = ofw_bus_string_list_to_array(node, "allwinner,pins",
	    &pinlist);
	if (*pins_nb > 0)
		return (pinlist);

	return (NULL);
}

static uint32_t
aw_gpio_parse_bias(phandle_t node)
{
	uint32_t bias;

	if (OF_getencprop(node, "pull", &bias, sizeof(bias)) != -1)
		return (bias);
	if (OF_getencprop(node, "allwinner,pull", &bias, sizeof(bias)) != -1)
		return (bias);
	if (OF_hasprop(node, "bias-disable"))
		return (AW_GPIO_NONE);
	if (OF_hasprop(node, "bias-pull-up"))
		return (AW_GPIO_PULLUP);
	if (OF_hasprop(node, "bias-pull-down"))
		return (AW_GPIO_PULLDOWN);

	return (AW_GPIO_NONE);
}

static int
aw_gpio_parse_drive_strength(phandle_t node, uint32_t *drive)
{
	uint32_t drive_str;

	if (OF_getencprop(node, "drive", drive, sizeof(*drive)) != -1)
		return (0);
	if (OF_getencprop(node, "allwinner,drive", drive, sizeof(*drive)) != -1)
		return (0);
	if (OF_getencprop(node, "drive-strength", &drive_str,
	    sizeof(drive_str)) != -1) {
		*drive = (drive_str / 10) - 1;
		return (0);
	}

	return (1);
}

static int
aw_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val)
{
	struct aw_gpio_softc *sc;
	int ret;

	sc = device_get_softc(dev);

	AW_GPIO_LOCK(sc);
	ret = aw_gpio_pin_get_locked(sc, pin, val);
	AW_GPIO_UNLOCK(sc);

	return (ret);
}

static int
aw_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct aw_gpio_softc *sc;
	uint32_t bank, data;

	sc = device_get_softc(dev);
	if (pin > sc->padconf->npins)
		return (EINVAL);

	bank = sc->padconf->pins[pin].port;
	pin = sc->padconf->pins[pin].pin;

	AW_GPIO_LOCK(sc);
	data = AW_GPIO_READ(sc, AW_GPIO_GP_DAT(bank));
	if (data & (1 << pin))
		data &= ~(1 << pin);
	else
		data |= (1 << pin);
	AW_GPIO_WRITE(sc, AW_GPIO_GP_DAT(bank), data);
	AW_GPIO_UNLOCK(sc);

	return (0);
}

static int
aw_gpio_pin_access_32(device_t dev, uint32_t first_pin, uint32_t clear_pins,
    uint32_t change_pins, uint32_t *orig_pins)
{
	struct aw_gpio_softc *sc;
	uint32_t bank, data, pin;

	sc = device_get_softc(dev);
	if (first_pin > sc->padconf->npins)
		return (EINVAL);

	/*
	 * We require that first_pin refers to the first pin in a bank, because
	 * this API is not about convenience, it's for making a set of pins
	 * change simultaneously (required) with reasonably high performance
	 * (desired); we need to do a read-modify-write on a single register.
	 */
	bank = sc->padconf->pins[first_pin].port;
	pin = sc->padconf->pins[first_pin].pin;
	if (pin != 0)
		return (EINVAL);

	AW_GPIO_LOCK(sc);
	data = AW_GPIO_READ(sc, AW_GPIO_GP_DAT(bank));
	if ((clear_pins | change_pins) != 0) 
		AW_GPIO_WRITE(sc, AW_GPIO_GP_DAT(bank),
		    (data & ~clear_pins) ^ change_pins);
	AW_GPIO_UNLOCK(sc);

	if (orig_pins != NULL)
		*orig_pins = data;

	return (0);
}

static int
aw_gpio_pin_config_32(device_t dev, uint32_t first_pin, uint32_t num_pins,
    uint32_t *pin_flags)
{
	struct aw_gpio_softc *sc;
	uint32_t bank, pin;
	int err;

	sc = device_get_softc(dev);
	if (first_pin > sc->padconf->npins)
		return (EINVAL);

	bank = sc->padconf->pins[first_pin].port;
	if (sc->padconf->pins[first_pin].pin != 0)
		return (EINVAL);

	/*
	 * The configuration for a bank of pins is scattered among several
	 * registers; we cannot g'tee to simultaneously change the state of all
	 * the pins in the flags array.  So just loop through the array
	 * configuring each pin for now.  If there was a strong need, it might
	 * be possible to support some limited simultaneous config, such as
	 * adjacent groups of 8 pins that line up the same as the config regs.
	 */
	for (err = 0, pin = first_pin; err == 0 && pin < num_pins; ++pin) {
		if (pin_flags[pin] & (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT))
			err = aw_gpio_pin_configure(sc, pin, pin_flags[pin]);
	}

	return (err);
}

static int
aw_find_pinnum_by_name(struct aw_gpio_softc *sc, const char *pinname)
{
	int i;

	for (i = 0; i < sc->padconf->npins; i++)
		if (!strcmp(pinname, sc->padconf->pins[i].name))
			return i;

	return (-1);
}

static int
aw_find_pin_func(struct aw_gpio_softc *sc, int pin, const char *func)
{
	int i;

	for (i = 0; i < AW_MAX_FUNC_BY_PIN; i++)
		if (sc->padconf->pins[pin].functions[i] &&
		    !strcmp(func, sc->padconf->pins[pin].functions[i]))
			return (i);

	return (-1);
}

static int
aw_fdt_configure_pins(device_t dev, phandle_t cfgxref)
{
	struct aw_gpio_softc *sc;
	phandle_t node;
	const char **pinlist = NULL;
	char *pin_function = NULL;
	uint32_t pin_drive, pin_pull;
	int pins_nb, pin_num, pin_func, i, ret;
	bool set_drive;

	sc = device_get_softc(dev);
	node = OF_node_from_xref(cfgxref);
	ret = 0;
	set_drive = false;

	/* Getting all prop for configuring pins */
	pinlist = aw_gpio_parse_pins(node, &pins_nb);
	if (pinlist == NULL)
		return (ENOENT);

	pin_function = aw_gpio_parse_function(node);
	if (pin_function == NULL) {
		ret = ENOENT;
		goto out;
	}

	if (aw_gpio_parse_drive_strength(node, &pin_drive) == 0)
		set_drive = true;

	pin_pull = aw_gpio_parse_bias(node);

	/* Configure each pin to the correct function, drive and pull */
	for (i = 0; i < pins_nb; i++) {
		pin_num = aw_find_pinnum_by_name(sc, pinlist[i]);
		if (pin_num == -1) {
			ret = ENOENT;
			goto out;
		}
		pin_func = aw_find_pin_func(sc, pin_num, pin_function);
		if (pin_func == -1) {
			ret = ENOENT;
			goto out;
		}

		AW_GPIO_LOCK(sc);

		if (aw_gpio_get_function(sc, pin_num) != pin_func)
			aw_gpio_set_function(sc, pin_num, pin_func);
		if (set_drive)
			aw_gpio_set_drv(sc, pin_num, pin_drive);
		if (pin_pull != AW_GPIO_NONE)
			aw_gpio_set_pud(sc, pin_num, pin_pull);

		AW_GPIO_UNLOCK(sc);
	}

 out:
	OF_prop_free(pinlist);
	OF_prop_free(pin_function);
	return (ret);
}

static int
aw_gpio_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner GPIO/Pinmux controller");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_gpio_attach(device_t dev)
{
	int rid, error;
	phandle_t gpio;
	struct aw_gpio_softc *sc;
	struct clk_list *clkp, *clkp_tmp;
	clk_t clk;
	hwreset_t rst = NULL;
	int off, err, clkret;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	mtx_init(&sc->sc_mtx, "aw gpio", "gpio", MTX_SPIN);

	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->sc_mem_res) {
		device_printf(dev, "cannot allocate memory window\n");
		goto fail;
	}

	sc->sc_bst = rman_get_bustag(sc->sc_mem_res);
	sc->sc_bsh = rman_get_bushandle(sc->sc_mem_res);

	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (!sc->sc_irq_res) {
		device_printf(dev, "cannot allocate interrupt\n");
		goto fail;
	}

	/* Find our node. */
	gpio = ofw_bus_get_node(sc->sc_dev);
	if (!OF_hasprop(gpio, "gpio-controller"))
		/* Node is not a GPIO controller. */
		goto fail;

	/* Use the right pin data for the current SoC */
	sc->padconf = (struct allwinner_padconf *)ofw_bus_search_compatible(dev,
	    compat_data)->ocd_data;

	if (hwreset_get_by_ofw_idx(dev, 0, 0, &rst) == 0) {
		error = hwreset_deassert(rst);
		if (error != 0) {
			device_printf(dev, "cannot de-assert reset\n");
			goto fail;
		}
	}

	TAILQ_INIT(&sc->clk_list);
	for (off = 0, clkret = 0; clkret == 0; off++) {
		clkret = clk_get_by_ofw_index(dev, 0, off, &clk);
		if (clkret != 0)
			break;
		err = clk_enable(clk);
		if (err != 0) {
			device_printf(dev, "Could not enable clock %s\n",
			    clk_get_name(clk));
			goto fail;
		}
		clkp = malloc(sizeof(*clkp), M_DEVBUF, M_WAITOK | M_ZERO);
		clkp->clk = clk;
		TAILQ_INSERT_TAIL(&sc->clk_list, clkp, next);
	}
	if (clkret != 0 && clkret != ENOENT) {
		device_printf(dev, "Could not find clock at offset %d (%d)\n",
		    off, clkret);
		goto fail;
	}

	sc->sc_busdev = gpiobus_attach_bus(dev);
	if (sc->sc_busdev == NULL)
		goto fail;

	/*
	 * Register as a pinctrl device
	 */
	fdt_pinctrl_register(dev, "pins");
	fdt_pinctrl_configure_tree(dev);
	fdt_pinctrl_register(dev, "allwinner,pins");
	fdt_pinctrl_configure_tree(dev);

	return (0);

fail:
	if (sc->sc_irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
	mtx_destroy(&sc->sc_mtx);

	/* Disable clock */
	TAILQ_FOREACH_SAFE(clkp, &sc->clk_list, next, clkp_tmp) {
		err = clk_disable(clkp->clk);
		if (err != 0)
			device_printf(dev, "Could not disable clock %s\n",
			    clk_get_name(clkp->clk));
		err = clk_release(clkp->clk);
		if (err != 0)
			device_printf(dev, "Could not release clock %s\n",
			    clk_get_name(clkp->clk));
		TAILQ_REMOVE(&sc->clk_list, clkp, next);
		free(clkp, M_DEVBUF);
	}

	/* Assert resets */
	if (rst) {
		hwreset_assert(rst);
		hwreset_release(rst);
	}

	return (ENXIO);
}

static int
aw_gpio_detach(device_t dev)
{

	return (EBUSY);
}

static phandle_t
aw_gpio_get_node(device_t dev, device_t bus)
{

	/* We only have one child, the GPIO bus, which needs our own node. */
	return (ofw_bus_get_node(dev));
}

static int
aw_gpio_map_gpios(device_t bus, phandle_t dev, phandle_t gparent, int gcells,
    pcell_t *gpios, uint32_t *pin, uint32_t *flags)
{
	struct aw_gpio_softc *sc;
	int i;

	sc = device_get_softc(bus);

	/* The GPIO pins are mapped as: <gpio-phandle bank pin flags>. */
	for (i = 0; i < sc->padconf->npins; i++)
		if (sc->padconf->pins[i].port == gpios[0] &&
		    sc->padconf->pins[i].pin == gpios[1]) {
			*pin = i;
			break;
		}
	*flags = gpios[gcells - 1];

	return (0);
}

static device_method_t aw_gpio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_gpio_probe),
	DEVMETHOD(device_attach,	aw_gpio_attach),
	DEVMETHOD(device_detach,	aw_gpio_detach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus,		aw_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		aw_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	aw_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags,	aw_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps,	aw_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags,	aw_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get,		aw_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,		aw_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,	aw_gpio_pin_toggle),
	DEVMETHOD(gpio_pin_access_32,	aw_gpio_pin_access_32),
	DEVMETHOD(gpio_pin_config_32,	aw_gpio_pin_config_32),
	DEVMETHOD(gpio_map_gpios,	aw_gpio_map_gpios),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	aw_gpio_get_node),

        /* fdt_pinctrl interface */
	DEVMETHOD(fdt_pinctrl_configure,aw_fdt_configure_pins),

	DEVMETHOD_END
};

static devclass_t aw_gpio_devclass;

static driver_t aw_gpio_driver = {
	"gpio",
	aw_gpio_methods,
	sizeof(struct aw_gpio_softc),
};

EARLY_DRIVER_MODULE(aw_gpio, simplebus, aw_gpio_driver, aw_gpio_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
