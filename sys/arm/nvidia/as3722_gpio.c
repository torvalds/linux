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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sx.h>

#include <machine/bus.h>

#include <dev/fdt/fdt_common.h>
#include <dev/gpio/gpiobusvar.h>

#include "as3722.h"

MALLOC_DEFINE(M_AS3722_GPIO, "AS3722 gpio", "AS3722 GPIO");

/* AS3722_GPIOx_CONTROL	 MODE and IOSF definition. */
#define	AS3722_IOSF_GPIO				0x00
#define	AS3722_IOSF_INTERRUPT_OUT			0x01
#define	AS3722_IOSF_VSUP_VBAT_LOW_UNDEBOUNCE_OUT	0x02
#define	AS3722_IOSF_GPIO_IN_INTERRUPT			0x03
#define	AS3722_IOSF_PWM_IN				0x04
#define	AS3722_IOSF_VOLTAGE_IN_STANDBY			0x05
#define	AS3722_IOSF_OC_PG_SD0				0x06
#define	AS3722_IOSF_POWERGOOD_OUT			0x07
#define	AS3722_IOSF_CLK32K_OUT				0x08
#define	AS3722_IOSF_WATCHDOG_IN				0x09
#define	AS3722_IOSF_SOFT_RESET_IN			0x0b
#define	AS3722_IOSF_PWM_OUT				0x0c
#define	AS3722_IOSF_VSUP_VBAT_LOW_DEBOUNCE_OUT		0x0d
#define	AS3722_IOSF_OC_PG_SD6				0x0e

#define	AS3722_MODE_INPUT				0
#define	AS3722_MODE_PUSH_PULL				1
#define	AS3722_MODE_OPEN_DRAIN				2
#define	AS3722_MODE_TRISTATE				3
#define	AS3722_MODE_INPUT_PULL_UP_LV			4
#define	AS3722_MODE_INPUT_PULL_DOWN			5
#define	AS3722_MODE_OPEN_DRAIN_LV			6
#define	AS3722_MODE_PUSH_PULL_LV			7

#define	NGPIO		8

#define	GPIO_LOCK(_sc)	sx_slock(&(_sc)->gpio_lock)
#define	GPIO_UNLOCK(_sc)	sx_unlock(&(_sc)->gpio_lock)
#define	GPIO_ASSERT(_sc)	sx_assert(&(_sc)->gpio_lock, SA_LOCKED)

#define	AS3722_CFG_BIAS_DISABLE		0x0001
#define	AS3722_CFG_BIAS_PULL_UP		0x0002
#define	AS3722_CFG_BIAS_PULL_DOWN	0x0004
#define	AS3722_CFG_BIAS_HIGH_IMPEDANCE	0x0008
#define	AS3722_CFG_OPEN_DRAIN		0x0010

static const struct {
	const char	*name;
	int  		config;		/* AS3722_CFG_  */
} as3722_cfg_names[] = {
	{"bias-disable",	AS3722_CFG_BIAS_DISABLE},
	{"bias-pull-up",	AS3722_CFG_BIAS_PULL_UP},
	{"bias-pull-down",	AS3722_CFG_BIAS_PULL_DOWN},
	{"bias-high-impedance",	AS3722_CFG_BIAS_HIGH_IMPEDANCE},
	{"drive-open-drain",	AS3722_CFG_OPEN_DRAIN},
};

static struct {
	const char *name;
	int fnc_val;
} as3722_fnc_table[] = {
	{"gpio",			AS3722_IOSF_GPIO},
	{"interrupt-out",		AS3722_IOSF_INTERRUPT_OUT},
	{"vsup-vbat-low-undebounce-out", AS3722_IOSF_VSUP_VBAT_LOW_UNDEBOUNCE_OUT},
	{"gpio-in-interrupt",		AS3722_IOSF_GPIO_IN_INTERRUPT},
	{"pwm-in",			AS3722_IOSF_PWM_IN},
	{"voltage-in-standby",		AS3722_IOSF_VOLTAGE_IN_STANDBY},
	{"oc-pg-sd0",			AS3722_IOSF_OC_PG_SD0},
	{"powergood-out",		AS3722_IOSF_POWERGOOD_OUT},
	{"clk32k-out",			AS3722_IOSF_CLK32K_OUT},
	{"watchdog-in",			AS3722_IOSF_WATCHDOG_IN},
	{"soft-reset-in",		AS3722_IOSF_SOFT_RESET_IN},
	{"pwm-out",			AS3722_IOSF_PWM_OUT},
	{"vsup-vbat-low-debounce-out",	AS3722_IOSF_VSUP_VBAT_LOW_DEBOUNCE_OUT},
	{"oc-pg-sd6",			AS3722_IOSF_OC_PG_SD6},
};

struct as3722_pincfg {
	char	*function;
	int	flags;
};

struct as3722_gpio_pin {
	int	pin_caps;
	uint8_t	pin_ctrl_reg;
	char	pin_name[GPIOMAXNAME];
	int	pin_cfg_flags;
};


/* --------------------------------------------------------------------------
 *
 *  Pinmux functions.
 */
static int
as3722_pinmux_get_function(struct as3722_softc *sc, char *name)
{
	int i;

	for (i = 0; i < nitems(as3722_fnc_table); i++) {
		if (strcmp(as3722_fnc_table[i].name, name) == 0)
			 return (as3722_fnc_table[i].fnc_val);
	}
	return (-1);
}



static int
as3722_pinmux_config_node(struct as3722_softc *sc, char *pin_name,
    struct as3722_pincfg *cfg)
{
	uint8_t ctrl;
	int rv, fnc, pin;

	for (pin = 0; pin < sc->gpio_npins; pin++) {
		if (strcmp(sc->gpio_pins[pin]->pin_name, pin_name) == 0)
			 break;
	}
	if (pin >= sc->gpio_npins) {
		device_printf(sc->dev, "Unknown pin: %s\n", pin_name);
		return (ENXIO);
	}

	ctrl = sc->gpio_pins[pin]->pin_ctrl_reg;
	sc->gpio_pins[pin]->pin_cfg_flags = cfg->flags;
	if (cfg->function != NULL) {
		fnc = as3722_pinmux_get_function(sc, cfg->function);
		if (fnc == -1) {
			device_printf(sc->dev,
			    "Unknown function %s for pin %s\n", cfg->function,
			    sc->gpio_pins[pin]->pin_name);
			return (ENXIO);
		}
		switch (fnc) {
		case AS3722_IOSF_INTERRUPT_OUT:
		case AS3722_IOSF_VSUP_VBAT_LOW_UNDEBOUNCE_OUT:
		case AS3722_IOSF_OC_PG_SD0:
		case AS3722_IOSF_POWERGOOD_OUT:
		case AS3722_IOSF_CLK32K_OUT:
		case AS3722_IOSF_PWM_OUT:
		case AS3722_IOSF_OC_PG_SD6:
			ctrl &= ~(AS3722_GPIO_MODE_MASK <<
			    AS3722_GPIO_MODE_SHIFT);
			ctrl |= AS3722_MODE_PUSH_PULL << AS3722_GPIO_MODE_SHIFT;
			/* XXX Handle flags (OC + pullup) */
			break;
		case AS3722_IOSF_GPIO_IN_INTERRUPT:
		case AS3722_IOSF_PWM_IN:
		case AS3722_IOSF_VOLTAGE_IN_STANDBY:
		case AS3722_IOSF_WATCHDOG_IN:
		case AS3722_IOSF_SOFT_RESET_IN:
			ctrl &= ~(AS3722_GPIO_MODE_MASK <<
			    AS3722_GPIO_MODE_SHIFT);
			ctrl |= AS3722_MODE_INPUT << AS3722_GPIO_MODE_SHIFT;
			/* XXX Handle flags (pulldown + pullup) */

		default:
			break;
		}
		ctrl &= ~(AS3722_GPIO_IOSF_MASK << AS3722_GPIO_IOSF_SHIFT);
		ctrl |= fnc << AS3722_GPIO_IOSF_SHIFT;
	}
	rv = 0;
	if (ctrl != sc->gpio_pins[pin]->pin_ctrl_reg) {
		rv = WR1(sc, AS3722_GPIO0_CONTROL + pin, ctrl);
		sc->gpio_pins[pin]->pin_ctrl_reg = ctrl;
	}
	return (rv);
}

static int
as3722_pinmux_read_node(struct as3722_softc *sc, phandle_t node,
     struct as3722_pincfg *cfg, char **pins, int *lpins)
{
	int rv, i;

	*lpins = OF_getprop_alloc(node, "pins", (void **)pins);
	if (*lpins <= 0)
		return (ENOENT);

	/* Read function (mux) settings. */
	rv = OF_getprop_alloc(node, "function", (void **)&cfg->function);
	if (rv <= 0)
		cfg->function = NULL;

	/* Read boolean properties. */
	for (i = 0; i < nitems(as3722_cfg_names); i++) {
		if (OF_hasprop(node, as3722_cfg_names[i].name))
			cfg->flags |= as3722_cfg_names[i].config;
	}
	return (0);
}

static int
as3722_pinmux_process_node(struct as3722_softc *sc, phandle_t node)
{
	struct as3722_pincfg cfg;
	char *pins, *pname;
	int i, len, lpins, rv;

	rv = as3722_pinmux_read_node(sc, node, &cfg, &pins, &lpins);
	if (rv != 0)
		return (rv);

	len = 0;
	pname = pins;
	do {
		i = strlen(pname) + 1;
		rv = as3722_pinmux_config_node(sc, pname, &cfg);
		if (rv != 0) {
			device_printf(sc->dev,
			    "Cannot configure pin: %s: %d\n", pname, rv);
		}
		len += i;
		pname += i;
	} while (len < lpins);

	if (pins != NULL)
		OF_prop_free(pins);
	if (cfg.function != NULL)
		OF_prop_free(cfg.function);

	return (rv);
}

int as3722_pinmux_configure(device_t dev, phandle_t cfgxref)
{
	struct as3722_softc *sc;
	phandle_t node, cfgnode;
	int rv;

	sc = device_get_softc(dev);
	cfgnode = OF_node_from_xref(cfgxref);

	for (node = OF_child(cfgnode); node != 0; node = OF_peer(node)) {
		if (!ofw_bus_node_status_okay(node))
			continue;
		rv = as3722_pinmux_process_node(sc, node);
		if (rv != 0)
			device_printf(dev, "Failed to process pinmux");

	}
	return (0);
}

/* --------------------------------------------------------------------------
 *
 *  GPIO
 */
device_t
as3722_gpio_get_bus(device_t dev)
{
	struct as3722_softc *sc;

	sc = device_get_softc(dev);
	return (sc->gpio_busdev);
}

int
as3722_gpio_pin_max(device_t dev, int *maxpin)
{

	*maxpin = NGPIO - 1;
	return (0);
}

int
as3722_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct as3722_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);
	GPIO_LOCK(sc);
	*caps = sc->gpio_pins[pin]->pin_caps;
	GPIO_UNLOCK(sc);
	return (0);
}

int
as3722_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct as3722_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);
	GPIO_LOCK(sc);
	memcpy(name, sc->gpio_pins[pin]->pin_name, GPIOMAXNAME);
	GPIO_UNLOCK(sc);
	return (0);
}

int
as3722_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *out_flags)
{
	struct as3722_softc *sc;
	uint8_t tmp, mode, iosf;
	uint32_t flags;
	bool inverted;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	tmp = sc->gpio_pins[pin]->pin_ctrl_reg;
	GPIO_UNLOCK(sc);
	iosf = (tmp >> AS3722_GPIO_IOSF_SHIFT) & AS3722_GPIO_IOSF_MASK;
	mode = (tmp >> AS3722_GPIO_MODE_SHIFT) & AS3722_GPIO_MODE_MASK;
	inverted = (tmp & AS3722_GPIO_INVERT) != 0;
	/* Is pin in GPIO mode ? */
	if (iosf != AS3722_IOSF_GPIO)
		return (ENXIO);

	flags = 0;
	switch (mode) {
	case AS3722_MODE_INPUT:
		flags = GPIO_PIN_INPUT;
		break;
	case AS3722_MODE_PUSH_PULL:
	case AS3722_MODE_PUSH_PULL_LV:
		flags = GPIO_PIN_OUTPUT;
		break;
	case AS3722_MODE_OPEN_DRAIN:
	case AS3722_MODE_OPEN_DRAIN_LV:
		flags = GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_OPENDRAIN;
		break;
	case AS3722_MODE_TRISTATE:
		flags = GPIO_PIN_TRISTATE;
		break;
	case AS3722_MODE_INPUT_PULL_UP_LV:
		flags = GPIO_PIN_INPUT | GPIO_PIN_PULLUP;
		break;

	case AS3722_MODE_INPUT_PULL_DOWN:
		flags = GPIO_PIN_OUTPUT | GPIO_PIN_PULLDOWN;
		break;
	}
	if (inverted)
		flags |= GPIO_PIN_INVIN | GPIO_PIN_INVOUT;
	*out_flags = flags;
	return (0);
}

static int
as3722_gpio_get_mode(struct as3722_softc *sc, uint32_t pin, uint32_t gpio_flags)
{
	uint8_t ctrl;
	int flags;

	ctrl = sc->gpio_pins[pin]->pin_ctrl_reg;
	flags =  sc->gpio_pins[pin]->pin_cfg_flags;

	/* Tristate mode. */
	if (flags & AS3722_CFG_BIAS_HIGH_IMPEDANCE ||
	    gpio_flags & GPIO_PIN_TRISTATE)
		return (AS3722_MODE_TRISTATE);

	/* Open drain modes. */
	if (flags & AS3722_CFG_OPEN_DRAIN || gpio_flags & GPIO_PIN_OPENDRAIN) {
		/* Only pull up have effect */
		if (flags & AS3722_CFG_BIAS_PULL_UP ||
		    gpio_flags & GPIO_PIN_PULLUP)
			return (AS3722_MODE_OPEN_DRAIN_LV);
		return (AS3722_MODE_OPEN_DRAIN);
	}
	/* Input modes. */
	if (gpio_flags & GPIO_PIN_INPUT) {
		/* Accept pull up or pull down. */
		if (flags & AS3722_CFG_BIAS_PULL_UP ||
		    gpio_flags & GPIO_PIN_PULLUP)
			return (AS3722_MODE_INPUT_PULL_UP_LV);

		if (flags & AS3722_CFG_BIAS_PULL_DOWN ||
		    gpio_flags & GPIO_PIN_PULLDOWN)
			return (AS3722_MODE_INPUT_PULL_DOWN);
		return (AS3722_MODE_INPUT);
	}
	/*
	 * Output modes.
	 * Pull down is used as indicator of low voltage output.
	 */
	if (flags & AS3722_CFG_BIAS_PULL_DOWN ||
		    gpio_flags & GPIO_PIN_PULLDOWN)
		return (AS3722_MODE_PUSH_PULL_LV);
	return (AS3722_MODE_PUSH_PULL);
}

int
as3722_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct as3722_softc *sc;
	uint8_t ctrl, mode, iosf;
	int rv;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	ctrl = sc->gpio_pins[pin]->pin_ctrl_reg;
	iosf = (ctrl >> AS3722_GPIO_IOSF_SHIFT) & AS3722_GPIO_IOSF_MASK;
	/* Is pin in GPIO mode ? */
	if (iosf != AS3722_IOSF_GPIO) {
		GPIO_UNLOCK(sc);
		return (ENXIO);
	}
	mode = as3722_gpio_get_mode(sc, pin, flags);
	ctrl &= ~(AS3722_GPIO_MODE_MASK << AS3722_GPIO_MODE_SHIFT);
	ctrl |= AS3722_MODE_PUSH_PULL << AS3722_GPIO_MODE_SHIFT;
	rv = 0;
	if (ctrl != sc->gpio_pins[pin]->pin_ctrl_reg) {
		rv = WR1(sc, AS3722_GPIO0_CONTROL + pin, ctrl);
		sc->gpio_pins[pin]->pin_ctrl_reg = ctrl;
	}
	GPIO_UNLOCK(sc);
	return (rv);
}

int
as3722_gpio_pin_set(device_t dev, uint32_t pin, uint32_t val)
{
	struct as3722_softc *sc;
	uint8_t tmp;
	int rv;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	tmp =  (val != 0) ? 1 : 0;
	if (sc->gpio_pins[pin]->pin_ctrl_reg & AS3722_GPIO_INVERT)
		tmp ^= 1;

	GPIO_LOCK(sc);
	rv = RM1(sc, AS3722_GPIO_SIGNAL_OUT, (1 << pin), (tmp << pin));
	GPIO_UNLOCK(sc);
	return (rv);
}

int
as3722_gpio_pin_get(device_t dev, uint32_t pin, uint32_t *val)
{
	struct as3722_softc *sc;
	uint8_t tmp, mode, ctrl;
	int rv;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	ctrl = sc->gpio_pins[pin]->pin_ctrl_reg;
	mode = (ctrl >> AS3722_GPIO_MODE_SHIFT) & AS3722_GPIO_MODE_MASK;
	if ((mode == AS3722_MODE_PUSH_PULL) ||
	    (mode == AS3722_MODE_PUSH_PULL_LV))
		rv = RD1(sc, AS3722_GPIO_SIGNAL_OUT, &tmp);
	else
		rv = RD1(sc, AS3722_GPIO_SIGNAL_IN, &tmp);
	GPIO_UNLOCK(sc);
	if (rv != 0)
		return (rv);

	*val = tmp & (1 << pin) ? 1 : 0;
	if (ctrl & AS3722_GPIO_INVERT)
		*val ^= 1;
	return (0);
}

int
as3722_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct as3722_softc *sc;
	uint8_t tmp;
	int rv;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	rv = RD1(sc, AS3722_GPIO_SIGNAL_OUT, &tmp);
	if (rv != 0) {
		GPIO_UNLOCK(sc);
		return (rv);
	}
	tmp ^= (1 <<pin);
	rv = RM1(sc, AS3722_GPIO_SIGNAL_OUT, (1 << pin), tmp);
	GPIO_UNLOCK(sc);
	return (0);
}

int
as3722_gpio_map_gpios(device_t dev, phandle_t pdev, phandle_t gparent,
    int gcells, pcell_t *gpios, uint32_t *pin, uint32_t *flags)
{

	if (gcells != 2)
		return (ERANGE);
	*pin = gpios[0];
	*flags= gpios[1];
	return (0);
}

int
as3722_gpio_attach(struct as3722_softc *sc, phandle_t node)
{
	struct as3722_gpio_pin *pin;
	int i, rv;

	sx_init(&sc->gpio_lock, "AS3722 GPIO lock");
	sc->gpio_npins = NGPIO;
	sc->gpio_pins = malloc(sizeof(struct as3722_gpio_pin *) *
	    sc->gpio_npins, M_AS3722_GPIO, M_WAITOK | M_ZERO);


	sc->gpio_busdev = gpiobus_attach_bus(sc->dev);
	if (sc->gpio_busdev == NULL)
		return (ENXIO);
	for (i = 0; i < sc->gpio_npins; i++) {
		sc->gpio_pins[i] = malloc(sizeof(struct as3722_gpio_pin),
		    M_AS3722_GPIO, M_WAITOK | M_ZERO);
		pin = sc->gpio_pins[i];
		sprintf(pin->pin_name, "gpio%d", i);
		pin->pin_caps = GPIO_PIN_INPUT | GPIO_PIN_OUTPUT  |
		    GPIO_PIN_OPENDRAIN | GPIO_PIN_PUSHPULL | GPIO_PIN_TRISTATE |
		    GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN | GPIO_PIN_INVIN |
		    GPIO_PIN_INVOUT;
		rv = RD1(sc, AS3722_GPIO0_CONTROL + i, &pin->pin_ctrl_reg);
		if (rv != 0) {
			device_printf(sc->dev,
			    "Cannot read configuration for pin %s\n",
			    sc->gpio_pins[i]->pin_name);
		}
	}
	return (0);
}
