/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * Copyright (c) 2012-2015 Luiz Otavio O Souza <loos@FreeBSD.org>
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

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/fdt/fdt_pinctrl.h>
#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/ofw_bus.h>

#include "gpio_if.h"

#include "pic_if.h"

#ifdef DEBUG
#define dprintf(fmt, args...) do { printf("%s(): ", __func__);   \
    printf(fmt,##args); } while (0)
#else
#define dprintf(fmt, args...)
#endif

#define	BCM_GPIO_IRQS		4
#define	BCM_GPIO_PINS		54
#define	BCM_GPIO_PINS_PER_BANK	32

#define	BCM_GPIO_DEFAULT_CAPS	(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT |	\
    GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN | GPIO_INTR_LEVEL_LOW |		\
    GPIO_INTR_LEVEL_HIGH | GPIO_INTR_EDGE_RISING |			\
    GPIO_INTR_EDGE_FALLING | GPIO_INTR_EDGE_BOTH)

#define	BCM2835_FSEL_GPIO_IN	0
#define	BCM2835_FSEL_GPIO_OUT	1
#define	BCM2835_FSEL_ALT5	2
#define	BCM2835_FSEL_ALT4	3
#define	BCM2835_FSEL_ALT0	4
#define	BCM2835_FSEL_ALT1	5
#define	BCM2835_FSEL_ALT2	6
#define	BCM2835_FSEL_ALT3	7

#define	BCM2835_PUD_OFF		0
#define	BCM2835_PUD_DOWN	1
#define	BCM2835_PUD_UP		2

static struct resource_spec bcm_gpio_res_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ SYS_RES_IRQ, 0, RF_ACTIVE },	/* bank 0 interrupt */
	{ SYS_RES_IRQ, 1, RF_ACTIVE },	/* bank 1 interrupt */
	{ -1, 0, 0 }
};

struct bcm_gpio_sysctl {
	struct bcm_gpio_softc	*sc;
	uint32_t		pin;
};

struct bcm_gpio_irqsrc {
	struct intr_irqsrc	bgi_isrc;
	uint32_t		bgi_irq;
	uint32_t		bgi_mode;
	uint32_t		bgi_mask;
};

struct bcm_gpio_softc {
	device_t		sc_dev;
	device_t		sc_busdev;
	struct mtx		sc_mtx;
	struct resource *	sc_res[BCM_GPIO_IRQS + 1];
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	void *			sc_intrhand[BCM_GPIO_IRQS];
	int			sc_gpio_npins;
	int			sc_ro_npins;
	int			sc_ro_pins[BCM_GPIO_PINS];
	struct gpio_pin		sc_gpio_pins[BCM_GPIO_PINS];
	struct bcm_gpio_sysctl	sc_sysctl[BCM_GPIO_PINS];
	struct bcm_gpio_irqsrc	sc_isrcs[BCM_GPIO_PINS];
};

enum bcm_gpio_pud {
	BCM_GPIO_NONE,
	BCM_GPIO_PULLDOWN,
	BCM_GPIO_PULLUP,
};

#define	BCM_GPIO_LOCK(_sc)	mtx_lock_spin(&(_sc)->sc_mtx)
#define	BCM_GPIO_UNLOCK(_sc)	mtx_unlock_spin(&(_sc)->sc_mtx)
#define	BCM_GPIO_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)
#define	BCM_GPIO_WRITE(_sc, _off, _val)		\
    bus_space_write_4((_sc)->sc_bst, (_sc)->sc_bsh, _off, _val)
#define	BCM_GPIO_READ(_sc, _off)		\
    bus_space_read_4((_sc)->sc_bst, (_sc)->sc_bsh, _off)
#define	BCM_GPIO_CLEAR_BITS(_sc, _off, _bits)	\
    BCM_GPIO_WRITE(_sc, _off, BCM_GPIO_READ(_sc, _off) & ~(_bits))
#define	BCM_GPIO_SET_BITS(_sc, _off, _bits)	\
    BCM_GPIO_WRITE(_sc, _off, BCM_GPIO_READ(_sc, _off) | _bits)
#define	BCM_GPIO_BANK(a)	(a / BCM_GPIO_PINS_PER_BANK)
#define	BCM_GPIO_MASK(a)	(1U << (a % BCM_GPIO_PINS_PER_BANK))

#define	BCM_GPIO_GPFSEL(_bank)	(0x00 + _bank * 4)	/* Function Select */
#define	BCM_GPIO_GPSET(_bank)	(0x1c + _bank * 4)	/* Pin Out Set */
#define	BCM_GPIO_GPCLR(_bank)	(0x28 + _bank * 4)	/* Pin Out Clear */
#define	BCM_GPIO_GPLEV(_bank)	(0x34 + _bank * 4)	/* Pin Level */
#define	BCM_GPIO_GPEDS(_bank)	(0x40 + _bank * 4)	/* Event Status */
#define	BCM_GPIO_GPREN(_bank)	(0x4c + _bank * 4)	/* Rising Edge irq */
#define	BCM_GPIO_GPFEN(_bank)	(0x58 + _bank * 4)	/* Falling Edge irq */
#define	BCM_GPIO_GPHEN(_bank)	(0x64 + _bank * 4)	/* High Level irq */
#define	BCM_GPIO_GPLEN(_bank)	(0x70 + _bank * 4)	/* Low Level irq */
#define	BCM_GPIO_GPAREN(_bank)	(0x7c + _bank * 4)	/* Async Rising Edge */
#define	BCM_GPIO_GPAFEN(_bank)	(0x88 + _bank * 4)	/* Async Falling Egde */
#define	BCM_GPIO_GPPUD(_bank)	(0x94)			/* Pin Pull up/down */
#define	BCM_GPIO_GPPUDCLK(_bank) (0x98 + _bank * 4)	/* Pin Pull up clock */

static struct ofw_compat_data compat_data[] = {
	{"broadcom,bcm2835-gpio",	1},
	{"brcm,bcm2835-gpio",		1},
	{NULL,				0}
};

static struct bcm_gpio_softc *bcm_gpio_sc = NULL;

static int bcm_gpio_intr_bank0(void *arg);
static int bcm_gpio_intr_bank1(void *arg);
static int bcm_gpio_pic_attach(struct bcm_gpio_softc *sc);
static int bcm_gpio_pic_detach(struct bcm_gpio_softc *sc);

static int
bcm_gpio_pin_is_ro(struct bcm_gpio_softc *sc, int pin)
{
	int i;

	for (i = 0; i < sc->sc_ro_npins; i++)
		if (pin == sc->sc_ro_pins[i])
			return (1);
	return (0);
}

static uint32_t
bcm_gpio_get_function(struct bcm_gpio_softc *sc, uint32_t pin)
{
	uint32_t bank, func, offset;

	/* Five banks, 10 pins per bank, 3 bits per pin. */
	bank = pin / 10;
	offset = (pin - bank * 10) * 3;

	BCM_GPIO_LOCK(sc);
	func = (BCM_GPIO_READ(sc, BCM_GPIO_GPFSEL(bank)) >> offset) & 7;
	BCM_GPIO_UNLOCK(sc);

	return (func);
}

static void
bcm_gpio_func_str(uint32_t nfunc, char *buf, int bufsize)
{

	switch (nfunc) {
	case BCM2835_FSEL_GPIO_IN:
		strncpy(buf, "input", bufsize);
		break;
	case BCM2835_FSEL_GPIO_OUT:
		strncpy(buf, "output", bufsize);
		break;
	case BCM2835_FSEL_ALT0:
		strncpy(buf, "alt0", bufsize);
		break;
	case BCM2835_FSEL_ALT1:
		strncpy(buf, "alt1", bufsize);
		break;
	case BCM2835_FSEL_ALT2:
		strncpy(buf, "alt2", bufsize);
		break;
	case BCM2835_FSEL_ALT3:
		strncpy(buf, "alt3", bufsize);
		break;
	case BCM2835_FSEL_ALT4:
		strncpy(buf, "alt4", bufsize);
		break;
	case BCM2835_FSEL_ALT5:
		strncpy(buf, "alt5", bufsize);
		break;
	default:
		strncpy(buf, "invalid", bufsize);
	}
}

static int
bcm_gpio_str_func(char *func, uint32_t *nfunc)
{

	if (strcasecmp(func, "input") == 0)
		*nfunc = BCM2835_FSEL_GPIO_IN;
	else if (strcasecmp(func, "output") == 0)
		*nfunc = BCM2835_FSEL_GPIO_OUT;
	else if (strcasecmp(func, "alt0") == 0)
		*nfunc = BCM2835_FSEL_ALT0;
	else if (strcasecmp(func, "alt1") == 0)
		*nfunc = BCM2835_FSEL_ALT1;
	else if (strcasecmp(func, "alt2") == 0)
		*nfunc = BCM2835_FSEL_ALT2;
	else if (strcasecmp(func, "alt3") == 0)
		*nfunc = BCM2835_FSEL_ALT3;
	else if (strcasecmp(func, "alt4") == 0)
		*nfunc = BCM2835_FSEL_ALT4;
	else if (strcasecmp(func, "alt5") == 0)
		*nfunc = BCM2835_FSEL_ALT5;
	else
		return (-1);

	return (0);
}

static uint32_t
bcm_gpio_func_flag(uint32_t nfunc)
{

	switch (nfunc) {
	case BCM2835_FSEL_GPIO_IN:
		return (GPIO_PIN_INPUT);
	case BCM2835_FSEL_GPIO_OUT:
		return (GPIO_PIN_OUTPUT);
	}
	return (0);
}

static void
bcm_gpio_set_function(struct bcm_gpio_softc *sc, uint32_t pin, uint32_t f)
{
	uint32_t bank, data, offset;

	/* Must be called with lock held. */
	BCM_GPIO_LOCK_ASSERT(sc);

	/* Five banks, 10 pins per bank, 3 bits per pin. */
	bank = pin / 10;
	offset = (pin - bank * 10) * 3;

	data = BCM_GPIO_READ(sc, BCM_GPIO_GPFSEL(bank));
	data &= ~(7 << offset);
	data |= (f << offset);
	BCM_GPIO_WRITE(sc, BCM_GPIO_GPFSEL(bank), data);
}

static void
bcm_gpio_set_pud(struct bcm_gpio_softc *sc, uint32_t pin, uint32_t state)
{
	uint32_t bank;

	/* Must be called with lock held. */
	BCM_GPIO_LOCK_ASSERT(sc);

	bank = BCM_GPIO_BANK(pin);
	BCM_GPIO_WRITE(sc, BCM_GPIO_GPPUD(0), state);
	BCM_GPIO_WRITE(sc, BCM_GPIO_GPPUDCLK(bank), BCM_GPIO_MASK(pin));
	BCM_GPIO_WRITE(sc, BCM_GPIO_GPPUD(0), 0);
	BCM_GPIO_WRITE(sc, BCM_GPIO_GPPUDCLK(bank), 0);
}

static void
bcm_gpio_set_alternate(device_t dev, uint32_t pin, uint32_t nfunc)
{
	struct bcm_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	BCM_GPIO_LOCK(sc);

	/* Set the pin function. */
	bcm_gpio_set_function(sc, pin, nfunc);

	/* Update the pin flags. */
	for (i = 0; i < sc->sc_gpio_npins; i++) {
		if (sc->sc_gpio_pins[i].gp_pin == pin)
			break;
	}
	if (i < sc->sc_gpio_npins)
		sc->sc_gpio_pins[i].gp_flags = bcm_gpio_func_flag(nfunc);

        BCM_GPIO_UNLOCK(sc);
}

static void
bcm_gpio_pin_configure(struct bcm_gpio_softc *sc, struct gpio_pin *pin,
    unsigned int flags)
{

	BCM_GPIO_LOCK(sc);

	/*
	 * Manage input/output.
	 */
	if (flags & (GPIO_PIN_INPUT|GPIO_PIN_OUTPUT)) {
		pin->gp_flags &= ~(GPIO_PIN_INPUT|GPIO_PIN_OUTPUT);
		if (flags & GPIO_PIN_OUTPUT) {
			pin->gp_flags |= GPIO_PIN_OUTPUT;
			bcm_gpio_set_function(sc, pin->gp_pin,
			    BCM2835_FSEL_GPIO_OUT);
		} else {
			pin->gp_flags |= GPIO_PIN_INPUT;
			bcm_gpio_set_function(sc, pin->gp_pin,
			    BCM2835_FSEL_GPIO_IN);
		}
	}

	/* Manage Pull-up/pull-down. */
	pin->gp_flags &= ~(GPIO_PIN_PULLUP|GPIO_PIN_PULLDOWN);
	if (flags & (GPIO_PIN_PULLUP|GPIO_PIN_PULLDOWN)) {
		if (flags & GPIO_PIN_PULLUP) {
			pin->gp_flags |= GPIO_PIN_PULLUP;
			bcm_gpio_set_pud(sc, pin->gp_pin, BCM_GPIO_PULLUP);
		} else {
			pin->gp_flags |= GPIO_PIN_PULLDOWN;
			bcm_gpio_set_pud(sc, pin->gp_pin, BCM_GPIO_PULLDOWN);
		}
	} else 
		bcm_gpio_set_pud(sc, pin->gp_pin, BCM_GPIO_NONE);

	BCM_GPIO_UNLOCK(sc);
}

static device_t
bcm_gpio_get_bus(device_t dev)
{
	struct bcm_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->sc_busdev);
}

static int
bcm_gpio_pin_max(device_t dev, int *maxpin)
{

	*maxpin = BCM_GPIO_PINS - 1;
	return (0);
}

static int
bcm_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	int i;

	for (i = 0; i < sc->sc_gpio_npins; i++) {
		if (sc->sc_gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->sc_gpio_npins)
		return (EINVAL);

	BCM_GPIO_LOCK(sc);
	*caps = sc->sc_gpio_pins[i].gp_caps;
	BCM_GPIO_UNLOCK(sc);

	return (0);
}

static int
bcm_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	int i;

	for (i = 0; i < sc->sc_gpio_npins; i++) {
		if (sc->sc_gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->sc_gpio_npins)
		return (EINVAL);

	BCM_GPIO_LOCK(sc);
	*flags = sc->sc_gpio_pins[i].gp_flags;
	BCM_GPIO_UNLOCK(sc);

	return (0);
}

static int
bcm_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	int i;

	for (i = 0; i < sc->sc_gpio_npins; i++) {
		if (sc->sc_gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->sc_gpio_npins)
		return (EINVAL);

	BCM_GPIO_LOCK(sc);
	memcpy(name, sc->sc_gpio_pins[i].gp_name, GPIOMAXNAME);
	BCM_GPIO_UNLOCK(sc);

	return (0);
}

static int
bcm_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	int i;

	for (i = 0; i < sc->sc_gpio_npins; i++) {
		if (sc->sc_gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->sc_gpio_npins)
		return (EINVAL);

	/* We never touch on read-only/reserved pins. */
	if (bcm_gpio_pin_is_ro(sc, pin))
		return (EINVAL);

	bcm_gpio_pin_configure(sc, &sc->sc_gpio_pins[i], flags);

	return (0);
}

static int
bcm_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	uint32_t bank, reg;
	int i;

	for (i = 0; i < sc->sc_gpio_npins; i++) {
		if (sc->sc_gpio_pins[i].gp_pin == pin)
			break;
	}
	if (i >= sc->sc_gpio_npins)
		return (EINVAL);
	/* We never write to read-only/reserved pins. */
	if (bcm_gpio_pin_is_ro(sc, pin))
		return (EINVAL);
	BCM_GPIO_LOCK(sc);
	bank = BCM_GPIO_BANK(pin);
	if (value)
		reg = BCM_GPIO_GPSET(bank);
	else
		reg = BCM_GPIO_GPCLR(bank);
	BCM_GPIO_WRITE(sc, reg, BCM_GPIO_MASK(pin));
	BCM_GPIO_UNLOCK(sc);

	return (0);
}

static int
bcm_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	uint32_t bank, reg_data;
	int i;

	for (i = 0; i < sc->sc_gpio_npins; i++) {
		if (sc->sc_gpio_pins[i].gp_pin == pin)
			break;
	}
	if (i >= sc->sc_gpio_npins)
		return (EINVAL);
	bank = BCM_GPIO_BANK(pin);
	BCM_GPIO_LOCK(sc);
	reg_data = BCM_GPIO_READ(sc, BCM_GPIO_GPLEV(bank));
	BCM_GPIO_UNLOCK(sc);
	*val = (reg_data & BCM_GPIO_MASK(pin)) ? 1 : 0;

	return (0);
}

static int
bcm_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	uint32_t bank, data, reg;
	int i;

	for (i = 0; i < sc->sc_gpio_npins; i++) {
		if (sc->sc_gpio_pins[i].gp_pin == pin)
			break;
	}
	if (i >= sc->sc_gpio_npins)
		return (EINVAL);
	/* We never write to read-only/reserved pins. */
	if (bcm_gpio_pin_is_ro(sc, pin))
		return (EINVAL);
	BCM_GPIO_LOCK(sc);
	bank = BCM_GPIO_BANK(pin);
	data = BCM_GPIO_READ(sc, BCM_GPIO_GPLEV(bank));
	if (data & BCM_GPIO_MASK(pin))
		reg = BCM_GPIO_GPCLR(bank);
	else
		reg = BCM_GPIO_GPSET(bank);
	BCM_GPIO_WRITE(sc, reg, BCM_GPIO_MASK(pin));
	BCM_GPIO_UNLOCK(sc);

	return (0);
}

static int
bcm_gpio_func_proc(SYSCTL_HANDLER_ARGS)
{
	char buf[16];
	struct bcm_gpio_softc *sc;
	struct bcm_gpio_sysctl *sc_sysctl;
	uint32_t nfunc;
	int error;

	sc_sysctl = arg1;
	sc = sc_sysctl->sc;

	/* Get the current pin function. */
	nfunc = bcm_gpio_get_function(sc, sc_sysctl->pin);
	bcm_gpio_func_str(nfunc, buf, sizeof(buf));

	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	/* Ignore changes on read-only pins. */
	if (bcm_gpio_pin_is_ro(sc, sc_sysctl->pin))
		return (0);
	/* Parse the user supplied string and check for a valid pin function. */
	if (bcm_gpio_str_func(buf, &nfunc) != 0)
		return (EINVAL);

	/* Update the pin alternate function. */
	bcm_gpio_set_alternate(sc->sc_dev, sc_sysctl->pin, nfunc);

	return (0);
}

static void
bcm_gpio_sysctl_init(struct bcm_gpio_softc *sc)
{
	char pinbuf[3];
	struct bcm_gpio_sysctl *sc_sysctl;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree_node, *pin_node, *pinN_node;
	struct sysctl_oid_list *tree, *pin_tree, *pinN_tree;
	int i;

	/*
	 * Add per-pin sysctl tree/handlers.
	 */
	ctx = device_get_sysctl_ctx(sc->sc_dev);
 	tree_node = device_get_sysctl_tree(sc->sc_dev);
 	tree = SYSCTL_CHILDREN(tree_node);
	pin_node = SYSCTL_ADD_NODE(ctx, tree, OID_AUTO, "pin",
	    CTLFLAG_RD, NULL, "GPIO Pins");
	pin_tree = SYSCTL_CHILDREN(pin_node);

	for (i = 0; i < sc->sc_gpio_npins; i++) {

		snprintf(pinbuf, sizeof(pinbuf), "%d", i);
		pinN_node = SYSCTL_ADD_NODE(ctx, pin_tree, OID_AUTO, pinbuf,
		    CTLFLAG_RD, NULL, "GPIO Pin");
		pinN_tree = SYSCTL_CHILDREN(pinN_node);

		sc->sc_sysctl[i].sc = sc;
		sc_sysctl = &sc->sc_sysctl[i];
		sc_sysctl->sc = sc;
		sc_sysctl->pin = sc->sc_gpio_pins[i].gp_pin;
		SYSCTL_ADD_PROC(ctx, pinN_tree, OID_AUTO, "function",
		    CTLFLAG_RW | CTLTYPE_STRING, sc_sysctl,
		    sizeof(struct bcm_gpio_sysctl), bcm_gpio_func_proc,
		    "A", "Pin Function");
	}
}

static int
bcm_gpio_get_ro_pins(struct bcm_gpio_softc *sc, phandle_t node,
	const char *propname, const char *label)
{
	int i, need_comma, npins, range_start, range_stop;
	pcell_t *pins;

	/* Get the property data. */
	npins = OF_getencprop_alloc_multi(node, propname, sizeof(*pins),
	    (void **)&pins);
	if (npins < 0)
		return (-1);
	if (npins == 0) {
		OF_prop_free(pins);
		return (0);
	}
	for (i = 0; i < npins; i++)
		sc->sc_ro_pins[i + sc->sc_ro_npins] = pins[i];
	sc->sc_ro_npins += npins;
	need_comma = 0;
	device_printf(sc->sc_dev, "%s pins: ", label);
	range_start = range_stop = pins[0];
	for (i = 1; i < npins; i++) {
		if (pins[i] != range_stop + 1) {
			if (need_comma)
				printf(",");
			if (range_start != range_stop)
				printf("%d-%d", range_start, range_stop);
			else
				printf("%d", range_start);
			range_start = range_stop = pins[i];
			need_comma = 1;
		} else
			range_stop++;
	}
	if (need_comma)
		printf(",");
	if (range_start != range_stop)
		printf("%d-%d.\n", range_start, range_stop);
	else
		printf("%d.\n", range_start);
	OF_prop_free(pins);

	return (0);
}

static int
bcm_gpio_get_reserved_pins(struct bcm_gpio_softc *sc)
{
	char *name;
	phandle_t gpio, node, reserved;
	ssize_t len;

	/* Get read-only pins if they're provided */
	gpio = ofw_bus_get_node(sc->sc_dev);
	if (bcm_gpio_get_ro_pins(sc, gpio, "broadcom,read-only",
	    "read-only") != 0)
		return (0);
	/* Traverse the GPIO subnodes to find the reserved pins node. */
	reserved = 0;
	node = OF_child(gpio);
	while ((node != 0) && (reserved == 0)) {
		len = OF_getprop_alloc(node, "name", (void **)&name);
		if (len == -1)
			return (-1);
		if (strcmp(name, "reserved") == 0)
			reserved = node;
		OF_prop_free(name);
		node = OF_peer(node);
	}
	if (reserved == 0)
		return (-1);
	/* Get the reserved pins. */
	if (bcm_gpio_get_ro_pins(sc, reserved, "broadcom,pins",
	    "reserved") != 0)
		return (-1);

	return (0);
}

static int
bcm_gpio_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "BCM2708/2835 GPIO controller");
	return (BUS_PROBE_DEFAULT);
}

static int
bcm_gpio_intr_attach(device_t dev)
{
	struct bcm_gpio_softc *sc;

	/*
	 *  Only first two interrupt lines are used. Third line is
	 *  mirrored second line and forth line is common for all banks.
	 */
	sc = device_get_softc(dev);
	if (sc->sc_res[1] == NULL || sc->sc_res[2] == NULL)
		return (-1);

	if (bcm_gpio_pic_attach(sc) != 0) {
		device_printf(dev, "unable to attach PIC\n");
		return (-1);
	}
	if (bus_setup_intr(dev, sc->sc_res[1], INTR_TYPE_MISC | INTR_MPSAFE,
	    bcm_gpio_intr_bank0, NULL, sc, &sc->sc_intrhand[0]) != 0)
		return (-1);
	if (bus_setup_intr(dev, sc->sc_res[2], INTR_TYPE_MISC | INTR_MPSAFE,
	    bcm_gpio_intr_bank1, NULL, sc, &sc->sc_intrhand[1]) != 0)
		return (-1);

	return (0);
}

static void
bcm_gpio_intr_detach(device_t dev)
{
	struct bcm_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (sc->sc_intrhand[0] != NULL)
		bus_teardown_intr(dev, sc->sc_res[1], sc->sc_intrhand[0]);
	if (sc->sc_intrhand[1] != NULL)
		bus_teardown_intr(dev, sc->sc_res[2], sc->sc_intrhand[1]);

	bcm_gpio_pic_detach(sc);
}

static int
bcm_gpio_attach(device_t dev)
{
	int i, j;
	phandle_t gpio;
	struct bcm_gpio_softc *sc;
	uint32_t func;

	if (bcm_gpio_sc != NULL)
		return (ENXIO);

	bcm_gpio_sc = sc = device_get_softc(dev);
 	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "bcm gpio", "gpio", MTX_SPIN);
	if (bus_alloc_resources(dev, bcm_gpio_res_spec, sc->sc_res) != 0) {
		device_printf(dev, "cannot allocate resources\n");
		goto fail;
	}
	sc->sc_bst = rman_get_bustag(sc->sc_res[0]);
	sc->sc_bsh = rman_get_bushandle(sc->sc_res[0]);
	/* Setup the GPIO interrupt handler. */
	if (bcm_gpio_intr_attach(dev)) {
		device_printf(dev, "unable to setup the gpio irq handler\n");
		goto fail;
	}
	/* Find our node. */
	gpio = ofw_bus_get_node(sc->sc_dev);
	if (!OF_hasprop(gpio, "gpio-controller"))
		/* Node is not a GPIO controller. */
		goto fail;
	/*
	 * Find the read-only pins.  These are pins we never touch or bad
	 * things could happen.
	 */
	if (bcm_gpio_get_reserved_pins(sc) == -1)
		goto fail;
	/* Initialize the software controlled pins. */
	for (i = 0, j = 0; j < BCM_GPIO_PINS; j++) {
		snprintf(sc->sc_gpio_pins[i].gp_name, GPIOMAXNAME,
		    "pin %d", j);
		func = bcm_gpio_get_function(sc, j);
		sc->sc_gpio_pins[i].gp_pin = j;
		sc->sc_gpio_pins[i].gp_caps = BCM_GPIO_DEFAULT_CAPS;
		sc->sc_gpio_pins[i].gp_flags = bcm_gpio_func_flag(func);
		i++;
	}
	sc->sc_gpio_npins = i;
	bcm_gpio_sysctl_init(sc);
	sc->sc_busdev = gpiobus_attach_bus(dev);
	if (sc->sc_busdev == NULL)
		goto fail;

	fdt_pinctrl_register(dev, "brcm,pins");
	fdt_pinctrl_configure_tree(dev);

	return (0);

fail:
	bcm_gpio_intr_detach(dev);
	bus_release_resources(dev, bcm_gpio_res_spec, sc->sc_res);
	mtx_destroy(&sc->sc_mtx);

	return (ENXIO);
}

static int
bcm_gpio_detach(device_t dev)
{

	return (EBUSY);
}

static inline void
bcm_gpio_modify(struct bcm_gpio_softc *sc, uint32_t reg, uint32_t mask,
    bool set_bits)
{

	if (set_bits)
		BCM_GPIO_SET_BITS(sc, reg, mask);
	else
		BCM_GPIO_CLEAR_BITS(sc, reg, mask);
}

static inline void
bcm_gpio_isrc_eoi(struct bcm_gpio_softc *sc, struct bcm_gpio_irqsrc *bgi)
{
	uint32_t bank;

	/* Write 1 to clear. */
	bank = BCM_GPIO_BANK(bgi->bgi_irq);
	BCM_GPIO_WRITE(sc, BCM_GPIO_GPEDS(bank), bgi->bgi_mask);
}

static inline bool
bcm_gpio_isrc_is_level(struct bcm_gpio_irqsrc *bgi)
{

	return (bgi->bgi_mode ==  GPIO_INTR_LEVEL_LOW ||
	    bgi->bgi_mode == GPIO_INTR_LEVEL_HIGH);
}

static inline void
bcm_gpio_isrc_mask(struct bcm_gpio_softc *sc, struct bcm_gpio_irqsrc *bgi)
{
	uint32_t bank;

	bank = BCM_GPIO_BANK(bgi->bgi_irq);
	BCM_GPIO_LOCK(sc);
	switch (bgi->bgi_mode) {
	case GPIO_INTR_LEVEL_LOW:
		BCM_GPIO_CLEAR_BITS(sc, BCM_GPIO_GPLEN(bank), bgi->bgi_mask);
		break;
	case GPIO_INTR_LEVEL_HIGH:
		BCM_GPIO_CLEAR_BITS(sc, BCM_GPIO_GPHEN(bank), bgi->bgi_mask);
		break;
	case GPIO_INTR_EDGE_RISING:
		BCM_GPIO_CLEAR_BITS(sc, BCM_GPIO_GPREN(bank), bgi->bgi_mask);
		break;
	case GPIO_INTR_EDGE_FALLING:
		BCM_GPIO_CLEAR_BITS(sc, BCM_GPIO_GPFEN(bank), bgi->bgi_mask);
		break;
	case GPIO_INTR_EDGE_BOTH:
		BCM_GPIO_CLEAR_BITS(sc, BCM_GPIO_GPREN(bank), bgi->bgi_mask);
		BCM_GPIO_CLEAR_BITS(sc, BCM_GPIO_GPFEN(bank), bgi->bgi_mask);
		break;
	}
	BCM_GPIO_UNLOCK(sc);
}

static inline void
bcm_gpio_isrc_unmask(struct bcm_gpio_softc *sc, struct bcm_gpio_irqsrc *bgi)
{
	uint32_t bank;

	bank = BCM_GPIO_BANK(bgi->bgi_irq);
	BCM_GPIO_LOCK(sc);
	switch (bgi->bgi_mode) {
	case GPIO_INTR_LEVEL_LOW:
		BCM_GPIO_SET_BITS(sc, BCM_GPIO_GPLEN(bank), bgi->bgi_mask);
		break;
	case GPIO_INTR_LEVEL_HIGH:
		BCM_GPIO_SET_BITS(sc, BCM_GPIO_GPHEN(bank), bgi->bgi_mask);
		break;
	case GPIO_INTR_EDGE_RISING:
		BCM_GPIO_SET_BITS(sc, BCM_GPIO_GPREN(bank), bgi->bgi_mask);
		break;
	case GPIO_INTR_EDGE_FALLING:
		BCM_GPIO_SET_BITS(sc, BCM_GPIO_GPFEN(bank), bgi->bgi_mask);
		break;
	case GPIO_INTR_EDGE_BOTH:
		BCM_GPIO_SET_BITS(sc, BCM_GPIO_GPREN(bank), bgi->bgi_mask);
		BCM_GPIO_SET_BITS(sc, BCM_GPIO_GPFEN(bank), bgi->bgi_mask);
		break;
	}
	BCM_GPIO_UNLOCK(sc);
}

static int
bcm_gpio_intr_internal(struct bcm_gpio_softc *sc, uint32_t bank)
{
	u_int irq;
	struct bcm_gpio_irqsrc *bgi;
	uint32_t reg;

	/* Do not care of spurious interrupt on GPIO. */
	reg = BCM_GPIO_READ(sc, BCM_GPIO_GPEDS(bank));
	while (reg != 0) {
		irq = BCM_GPIO_PINS_PER_BANK * bank + ffs(reg) - 1;
		bgi = sc->sc_isrcs + irq;
		if (!bcm_gpio_isrc_is_level(bgi))
			bcm_gpio_isrc_eoi(sc, bgi);
		if (intr_isrc_dispatch(&bgi->bgi_isrc,
		    curthread->td_intr_frame) != 0) {
			bcm_gpio_isrc_mask(sc, bgi);
			if (bcm_gpio_isrc_is_level(bgi))
				bcm_gpio_isrc_eoi(sc, bgi);
			device_printf(sc->sc_dev, "Stray irq %u disabled\n",
			    irq);
		}
		reg &= ~bgi->bgi_mask;
	}
	return (FILTER_HANDLED);
}

static int
bcm_gpio_intr_bank0(void *arg)
{

	return (bcm_gpio_intr_internal(arg, 0));
}

static int
bcm_gpio_intr_bank1(void *arg)
{

	return (bcm_gpio_intr_internal(arg, 1));
}

static int
bcm_gpio_pic_attach(struct bcm_gpio_softc *sc)
{
	int error;
	uint32_t irq;
	const char *name;

	name = device_get_nameunit(sc->sc_dev);
	for (irq = 0; irq < BCM_GPIO_PINS; irq++) {
		sc->sc_isrcs[irq].bgi_irq = irq;
		sc->sc_isrcs[irq].bgi_mask = BCM_GPIO_MASK(irq);
		sc->sc_isrcs[irq].bgi_mode = GPIO_INTR_CONFORM;

		error = intr_isrc_register(&sc->sc_isrcs[irq].bgi_isrc,
		    sc->sc_dev, 0, "%s,%u", name, irq);
		if (error != 0)
			return (error); /* XXX deregister ISRCs */
	}
	if (intr_pic_register(sc->sc_dev,
	    OF_xref_from_node(ofw_bus_get_node(sc->sc_dev))) == NULL)
		return (ENXIO);

	return (0);
}

static int
bcm_gpio_pic_detach(struct bcm_gpio_softc *sc)
{

	/*
	 *  There has not been established any procedure yet
	 *  how to detach PIC from living system correctly.
	 */
	device_printf(sc->sc_dev, "%s: not implemented yet\n", __func__);
	return (EBUSY);
}

static void
bcm_gpio_pic_config_intr(struct bcm_gpio_softc *sc, struct bcm_gpio_irqsrc *bgi,
    uint32_t mode)
{
	uint32_t bank;

	bank = BCM_GPIO_BANK(bgi->bgi_irq);
	BCM_GPIO_LOCK(sc);
	bcm_gpio_modify(sc, BCM_GPIO_GPREN(bank), bgi->bgi_mask,
	    mode == GPIO_INTR_EDGE_RISING || mode == GPIO_INTR_EDGE_BOTH);
	bcm_gpio_modify(sc, BCM_GPIO_GPFEN(bank), bgi->bgi_mask,
	    mode == GPIO_INTR_EDGE_FALLING || mode == GPIO_INTR_EDGE_BOTH);
	bcm_gpio_modify(sc, BCM_GPIO_GPHEN(bank), bgi->bgi_mask,
	    mode == GPIO_INTR_LEVEL_HIGH);
	bcm_gpio_modify(sc, BCM_GPIO_GPLEN(bank), bgi->bgi_mask,
	    mode == GPIO_INTR_LEVEL_LOW);
	bgi->bgi_mode = mode;
	BCM_GPIO_UNLOCK(sc);
}

static void
bcm_gpio_pic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	struct bcm_gpio_irqsrc *bgi = (struct bcm_gpio_irqsrc *)isrc;

	bcm_gpio_isrc_mask(sc, bgi);
}

static void
bcm_gpio_pic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	struct bcm_gpio_irqsrc *bgi = (struct bcm_gpio_irqsrc *)isrc;

	arm_irq_memory_barrier(bgi->bgi_irq);
	bcm_gpio_isrc_unmask(sc, bgi);
}

static int
bcm_gpio_pic_map_fdt(struct bcm_gpio_softc *sc, struct intr_map_data_fdt *daf,
    u_int *irqp, uint32_t *modep)
{
	u_int irq;
	uint32_t mode;

	/*
	 * The first cell is the interrupt number.
	 * The second cell is used to specify flags:
	 *	bits[3:0] trigger type and level flags:
	 *		1 = low-to-high edge triggered.
	 *		2 = high-to-low edge triggered.
	 *		4 = active high level-sensitive.
	 *		8 = active low level-sensitive.
	 */
	if (daf->ncells != 2)
		return (EINVAL);

	irq = daf->cells[0];
	if (irq >= BCM_GPIO_PINS || bcm_gpio_pin_is_ro(sc, irq))
		return (EINVAL);

	/* Only reasonable modes are supported. */
	if (daf->cells[1] == 1)
		mode = GPIO_INTR_EDGE_RISING;
	else if (daf->cells[1] == 2)
		mode = GPIO_INTR_EDGE_FALLING;
	else if (daf->cells[1] == 3)
		mode = GPIO_INTR_EDGE_BOTH;
	else if (daf->cells[1] == 4)
		mode = GPIO_INTR_LEVEL_HIGH;
	else if (daf->cells[1] == 8)
		mode = GPIO_INTR_LEVEL_LOW;
	else
		return (EINVAL);

	*irqp = irq;
	if (modep != NULL)
		*modep = mode;
	return (0);
}

static int
bcm_gpio_pic_map_gpio(struct bcm_gpio_softc *sc, struct intr_map_data_gpio *dag,
    u_int *irqp, uint32_t *modep)
{
	u_int irq;
	uint32_t mode;

	irq = dag->gpio_pin_num;
	if (irq >= BCM_GPIO_PINS || bcm_gpio_pin_is_ro(sc, irq))
		return (EINVAL);

	mode = dag->gpio_intr_mode;
	if (mode != GPIO_INTR_LEVEL_LOW && mode != GPIO_INTR_LEVEL_HIGH &&
	    mode != GPIO_INTR_EDGE_RISING && mode != GPIO_INTR_EDGE_FALLING &&
	    mode != GPIO_INTR_EDGE_BOTH)
		return (EINVAL);

	*irqp = irq;
	if (modep != NULL)
		*modep = mode;
	return (0);
}

static int
bcm_gpio_pic_map(struct bcm_gpio_softc *sc, struct intr_map_data *data,
    u_int *irqp, uint32_t *modep)
{

	switch (data->type) {
	case INTR_MAP_DATA_FDT:
		return (bcm_gpio_pic_map_fdt(sc,
		    (struct intr_map_data_fdt *)data, irqp, modep));
	case INTR_MAP_DATA_GPIO:
		return (bcm_gpio_pic_map_gpio(sc,
		    (struct intr_map_data_gpio *)data, irqp, modep));
	default:
		return (ENOTSUP);
	}
}

static int
bcm_gpio_pic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	int error;
	u_int irq;
	struct bcm_gpio_softc *sc = device_get_softc(dev);

	error = bcm_gpio_pic_map(sc, data, &irq, NULL);
	if (error == 0)
		*isrcp = &sc->sc_isrcs[irq].bgi_isrc;
	return (error);
}

static void
bcm_gpio_pic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	struct bcm_gpio_irqsrc *bgi = (struct bcm_gpio_irqsrc *)isrc;

	if (bcm_gpio_isrc_is_level(bgi))
		bcm_gpio_isrc_eoi(sc, bgi);
}

static void
bcm_gpio_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	bcm_gpio_pic_enable_intr(dev, isrc);
}

static void
bcm_gpio_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	struct bcm_gpio_irqsrc *bgi = (struct bcm_gpio_irqsrc *)isrc;

	bcm_gpio_isrc_mask(sc, bgi);
	if (bcm_gpio_isrc_is_level(bgi))
		bcm_gpio_isrc_eoi(sc, bgi);
}

static int
bcm_gpio_pic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	u_int irq;
	uint32_t mode;
	struct bcm_gpio_softc *sc;
	struct bcm_gpio_irqsrc *bgi;

	if (data == NULL)
		return (ENOTSUP);

	sc = device_get_softc(dev);
	bgi = (struct bcm_gpio_irqsrc *)isrc;

	/* Get and check config for an interrupt. */
	if (bcm_gpio_pic_map(sc, data, &irq, &mode) != 0 || bgi->bgi_irq != irq)
		return (EINVAL);

	/*
	 * If this is a setup for another handler,
	 * only check that its configuration match.
	 */
	if (isrc->isrc_handlers != 0)
		return (bgi->bgi_mode == mode ? 0 : EINVAL);

	bcm_gpio_pic_config_intr(sc, bgi, mode);
	return (0);
}

static int
bcm_gpio_pic_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct bcm_gpio_softc *sc = device_get_softc(dev);
	struct bcm_gpio_irqsrc *bgi = (struct bcm_gpio_irqsrc *)isrc;

	if (isrc->isrc_handlers == 0)
		bcm_gpio_pic_config_intr(sc, bgi, GPIO_INTR_CONFORM);
	return (0);
}

static phandle_t
bcm_gpio_get_node(device_t bus, device_t dev)
{

	/* We only have one child, the GPIO bus, which needs our own node. */
	return (ofw_bus_get_node(bus));
}

static int
bcm_gpio_configure_pins(device_t dev, phandle_t cfgxref)
{
	phandle_t cfgnode;
	int i, pintuples, pulltuples;
	uint32_t pin;
	uint32_t *pins;
	uint32_t *pulls;
	uint32_t function;
	static struct bcm_gpio_softc *sc;

	sc = device_get_softc(dev);
	cfgnode = OF_node_from_xref(cfgxref);

	pins = NULL;
	pintuples = OF_getencprop_alloc_multi(cfgnode, "brcm,pins",
	    sizeof(*pins), (void **)&pins);

	char name[32];
	OF_getprop(cfgnode, "name", &name, sizeof(name));

	if (pintuples < 0)
		return (ENOENT);

	if (pintuples == 0)
		return (0); /* Empty property is not an error. */

	if (OF_getencprop(cfgnode, "brcm,function", &function,
	    sizeof(function)) <= 0) {
		OF_prop_free(pins);
		return (EINVAL);
	}

	pulls = NULL;
	pulltuples = OF_getencprop_alloc_multi(cfgnode, "brcm,pull",
	    sizeof(*pulls), (void **)&pulls);

	if ((pulls != NULL) && (pulltuples != pintuples)) {
		OF_prop_free(pins);
		OF_prop_free(pulls);
		return (EINVAL);
	}

	for (i = 0; i < pintuples; i++) {
		pin = pins[i];
		bcm_gpio_set_alternate(dev, pin, function);
		if (bootverbose)
			device_printf(dev, "set pin %d to func %d", pin, function);
		if (pulls) {
			if (bootverbose)
				printf(", pull %d", pulls[i]);
			switch (pulls[i]) {
			/* Convert to gpio(4) flags */
			case BCM2835_PUD_OFF:
				bcm_gpio_pin_setflags(dev, pin, 0);
				break;
			case BCM2835_PUD_UP:
				bcm_gpio_pin_setflags(dev, pin, GPIO_PIN_PULLUP);
				break;
			case BCM2835_PUD_DOWN:
				bcm_gpio_pin_setflags(dev, pin, GPIO_PIN_PULLDOWN);
				break;
			default:
				printf("%s: invalid pull value for pin %d: %d\n",
				    name, pin, pulls[i]);
			}
		}
		if (bootverbose)
			printf("\n");
	}

	OF_prop_free(pins);
	if (pulls)
		OF_prop_free(pulls);

	return (0);
}

static device_method_t bcm_gpio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bcm_gpio_probe),
	DEVMETHOD(device_attach,	bcm_gpio_attach),
	DEVMETHOD(device_detach,	bcm_gpio_detach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus,		bcm_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		bcm_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	bcm_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags,	bcm_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps,	bcm_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags,	bcm_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get,		bcm_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,		bcm_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,	bcm_gpio_pin_toggle),

	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	bcm_gpio_pic_disable_intr),
	DEVMETHOD(pic_enable_intr,	bcm_gpio_pic_enable_intr),
	DEVMETHOD(pic_map_intr,		bcm_gpio_pic_map_intr),
	DEVMETHOD(pic_post_filter,	bcm_gpio_pic_post_filter),
	DEVMETHOD(pic_post_ithread,	bcm_gpio_pic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	bcm_gpio_pic_pre_ithread),
	DEVMETHOD(pic_setup_intr,	bcm_gpio_pic_setup_intr),
	DEVMETHOD(pic_teardown_intr,	bcm_gpio_pic_teardown_intr),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	bcm_gpio_get_node),

        /* fdt_pinctrl interface */
	DEVMETHOD(fdt_pinctrl_configure, bcm_gpio_configure_pins),

	DEVMETHOD_END
};

static devclass_t bcm_gpio_devclass;

static driver_t bcm_gpio_driver = {
	"gpio",
	bcm_gpio_methods,
	sizeof(struct bcm_gpio_softc),
};

EARLY_DRIVER_MODULE(bcm_gpio, simplebus, bcm_gpio_driver, bcm_gpio_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
