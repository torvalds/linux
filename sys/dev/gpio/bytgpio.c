/*-
 * Copyright (c) 2016 Oleksandr Tymoshenko <gonzo@FreeBSD.org>
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

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>
#include <dev/gpio/gpiobusvar.h>

#include "gpio_if.h"

/**
 *	Macros for driver mutex locking
 */
#define	BYTGPIO_LOCK(_sc)		mtx_lock_spin(&(_sc)->sc_mtx)
#define	BYTGPIO_UNLOCK(_sc)		mtx_unlock_spin(&(_sc)->sc_mtx)
#define	BYTGPIO_LOCK_INIT(_sc)		\
	mtx_init(&_sc->sc_mtx, device_get_nameunit((_sc)->sc_dev), \
	    "bytgpio", MTX_SPIN)
#define	BYTGPIO_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx)
#define	BYTGPIO_ASSERT_LOCKED(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)
#define	BYTGPIO_ASSERT_UNLOCKED(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_NOTOWNED)

struct pinmap_info {
    int reg;
    int pad_func;
};

/* Ignore function check, no info is available at the moment */
#define	PADCONF_FUNC_ANY	-1

#define	GPIO_PIN_MAP(r, f) { .reg = (r), .pad_func = (f) }

struct bytgpio_softc {
	ACPI_HANDLE		sc_handle;
	device_t		sc_dev;
	device_t		sc_busdev;
	struct mtx		sc_mtx;
	int			sc_mem_rid;
	struct resource		*sc_mem_res;
	int			sc_npins;
	const char*		sc_bank_prefix;
	const struct pinmap_info	*sc_pinpad_map;
	/* List of current functions for pads shared by GPIO */
	int			*sc_pad_funcs;
};

static int	bytgpio_probe(device_t dev);
static int	bytgpio_attach(device_t dev);
static int	bytgpio_detach(device_t dev);

#define	SCORE_UID		1
#define	SCORE_BANK_PREFIX	"GPIO_S0_SC"
const struct pinmap_info bytgpio_score_pins[] = {
	GPIO_PIN_MAP(85, 0),
	GPIO_PIN_MAP(89, 0),
	GPIO_PIN_MAP(93, 0),
	GPIO_PIN_MAP(96, 0),
	GPIO_PIN_MAP(99, 0),
	GPIO_PIN_MAP(102, 0),
	GPIO_PIN_MAP(98, 0),
	GPIO_PIN_MAP(101, 0),
	GPIO_PIN_MAP(34, 0),
	GPIO_PIN_MAP(37, 0),
	GPIO_PIN_MAP(36, 0),
	GPIO_PIN_MAP(38, 0),
	GPIO_PIN_MAP(39, 0),
	GPIO_PIN_MAP(35, 0),
	GPIO_PIN_MAP(40, 0),
	GPIO_PIN_MAP(84, 0),
	GPIO_PIN_MAP(62, 0),
	GPIO_PIN_MAP(61, 0),
	GPIO_PIN_MAP(64, 0),
	GPIO_PIN_MAP(59, 0),
	GPIO_PIN_MAP(54, 0),
	GPIO_PIN_MAP(56, 0),
	GPIO_PIN_MAP(60, 0),
	GPIO_PIN_MAP(55, 0),
	GPIO_PIN_MAP(63, 0),
	GPIO_PIN_MAP(57, 0),
	GPIO_PIN_MAP(51, 0),
	GPIO_PIN_MAP(50, 0),
	GPIO_PIN_MAP(53, 0),
	GPIO_PIN_MAP(47, 0),
	GPIO_PIN_MAP(52, 0),
	GPIO_PIN_MAP(49, 0),
	GPIO_PIN_MAP(48, 0),
	GPIO_PIN_MAP(43, 0),
	GPIO_PIN_MAP(46, 0),
	GPIO_PIN_MAP(41, 0),
	GPIO_PIN_MAP(45, 0),
	GPIO_PIN_MAP(42, 0),
	GPIO_PIN_MAP(58, 0),
	GPIO_PIN_MAP(44, 0),
	GPIO_PIN_MAP(95, 0),
	GPIO_PIN_MAP(105, 0),
	GPIO_PIN_MAP(70, 0),
	GPIO_PIN_MAP(68, 0),
	GPIO_PIN_MAP(67, 0),
	GPIO_PIN_MAP(66, 0),
	GPIO_PIN_MAP(69, 0),
	GPIO_PIN_MAP(71, 0),
	GPIO_PIN_MAP(65, 0),
	GPIO_PIN_MAP(72, 0),
	GPIO_PIN_MAP(86, 0),
	GPIO_PIN_MAP(90, 0),
	GPIO_PIN_MAP(88, 0),
	GPIO_PIN_MAP(92, 0),
	GPIO_PIN_MAP(103, 0),
	GPIO_PIN_MAP(77, 0),
	GPIO_PIN_MAP(79, 0),
	GPIO_PIN_MAP(83, 0),
	GPIO_PIN_MAP(78, 0),
	GPIO_PIN_MAP(81, 0),
	GPIO_PIN_MAP(80, 0),
	GPIO_PIN_MAP(82, 0),
	GPIO_PIN_MAP(13, 0),
	GPIO_PIN_MAP(12, 0),
	GPIO_PIN_MAP(15, 0),
	GPIO_PIN_MAP(14, 0),
	GPIO_PIN_MAP(17, 0),
	GPIO_PIN_MAP(18, 0),
	GPIO_PIN_MAP(19, 0),
	GPIO_PIN_MAP(16, 0),
	GPIO_PIN_MAP(2, 0),
	GPIO_PIN_MAP(1, 0),
	GPIO_PIN_MAP(0, 0),
	GPIO_PIN_MAP(4, 0),
	GPIO_PIN_MAP(6, 0),
	GPIO_PIN_MAP(7, 0),
	GPIO_PIN_MAP(9, 0),
	GPIO_PIN_MAP(8, 0),
	GPIO_PIN_MAP(33, 0),
	GPIO_PIN_MAP(32, 0),
	GPIO_PIN_MAP(31, 0),
	GPIO_PIN_MAP(30, 0),
	GPIO_PIN_MAP(29, 0),
	GPIO_PIN_MAP(27, 0),
	GPIO_PIN_MAP(25, 0),
	GPIO_PIN_MAP(28, 0),
	GPIO_PIN_MAP(26, 0),
	GPIO_PIN_MAP(23, 0),
	GPIO_PIN_MAP(21, 0),
	GPIO_PIN_MAP(20, 0),
	GPIO_PIN_MAP(24, 0),
	GPIO_PIN_MAP(22, 0),
	GPIO_PIN_MAP(5, 1),
	GPIO_PIN_MAP(3, 1),
	GPIO_PIN_MAP(10, 0),
	GPIO_PIN_MAP(11, 0),
	GPIO_PIN_MAP(106, 0),
	GPIO_PIN_MAP(87, 0),
	GPIO_PIN_MAP(91, 0),
	GPIO_PIN_MAP(104, 0),
	GPIO_PIN_MAP(97, 0),
	GPIO_PIN_MAP(100, 0)
};

#define	SCORE_PINS	nitems(bytgpio_score_pins)

#define	NCORE_UID		2
#define	NCORE_BANK_PREFIX	"GPIO_S0_NC"
const struct pinmap_info bytgpio_ncore_pins[] = {
	GPIO_PIN_MAP(19, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(18, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(17, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(20, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(21, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(22, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(24, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(25, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(23, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(16, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(14, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(15, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(12, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(26, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(27, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(1, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(4, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(8, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(11, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(0, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(3, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(6, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(10, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(13, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(2, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(5, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(9, PADCONF_FUNC_ANY),
	GPIO_PIN_MAP(7, PADCONF_FUNC_ANY)
};
#define	NCORE_PINS	nitems(bytgpio_ncore_pins)

#define	SUS_UID		3
#define	SUS_BANK_PREFIX	"GPIO_S5_"
const struct pinmap_info bytgpio_sus_pins[] = {
	GPIO_PIN_MAP(29, 0),
	GPIO_PIN_MAP(33, 0),
	GPIO_PIN_MAP(30, 0),
	GPIO_PIN_MAP(31, 0),
	GPIO_PIN_MAP(32, 0),
	GPIO_PIN_MAP(34, 0),
	GPIO_PIN_MAP(36, 0),
	GPIO_PIN_MAP(35, 0),
	GPIO_PIN_MAP(38, 0),
	GPIO_PIN_MAP(37, 0),
	GPIO_PIN_MAP(18, 0),
	GPIO_PIN_MAP(7, 1),
	GPIO_PIN_MAP(11, 1),
	GPIO_PIN_MAP(20, 1),
	GPIO_PIN_MAP(17, 1),
	GPIO_PIN_MAP(1, 1),
	GPIO_PIN_MAP(8, 1),
	GPIO_PIN_MAP(10, 1),
	GPIO_PIN_MAP(19, 1),
	GPIO_PIN_MAP(12, 1),
	GPIO_PIN_MAP(0, 1),
	GPIO_PIN_MAP(2, 1),
	GPIO_PIN_MAP(23, 0),
	GPIO_PIN_MAP(39, 0),
	GPIO_PIN_MAP(28, 0),
	GPIO_PIN_MAP(27, 0),
	GPIO_PIN_MAP(22, 0),
	GPIO_PIN_MAP(21, 0),
	GPIO_PIN_MAP(24, 0),
	GPIO_PIN_MAP(25, 0),
	GPIO_PIN_MAP(26, 0),
	GPIO_PIN_MAP(51, 0),
	GPIO_PIN_MAP(56, 0),
	GPIO_PIN_MAP(54, 0),
	GPIO_PIN_MAP(49, 0),
	GPIO_PIN_MAP(55, 0),
	GPIO_PIN_MAP(48, 0),
	GPIO_PIN_MAP(57, 0),
	GPIO_PIN_MAP(50, 0),
	GPIO_PIN_MAP(58, 0),
	GPIO_PIN_MAP(52, 0),
	GPIO_PIN_MAP(53, 0),
	GPIO_PIN_MAP(59, 0),
	GPIO_PIN_MAP(40, 0)
};

#define	SUS_PINS	nitems(bytgpio_sus_pins)

#define	BYGPIO_PIN_REGISTER(sc, pin, r)	((sc)->sc_pinpad_map[(pin)].reg * 16 + (r))
#define	BYTGPIO_PCONF0		0x0000
#define		BYTGPIO_PCONF0_FUNC_MASK	7
#define	BYTGPIO_PAD_VAL		0x0008
#define		BYTGPIO_PAD_VAL_LEVEL		(1 << 0)	
#define		BYTGPIO_PAD_VAL_I_OUTPUT_ENABLED	(1 << 1)
#define		BYTGPIO_PAD_VAL_I_INPUT_ENABLED	(1 << 2)
#define		BYTGPIO_PAD_VAL_DIR_MASK		(3 << 1)

static inline uint32_t
bytgpio_read_4(struct bytgpio_softc *sc, bus_size_t off)
{
	return (bus_read_4(sc->sc_mem_res, off));
}

static inline void
bytgpio_write_4(struct bytgpio_softc *sc, bus_size_t off,
    uint32_t val)
{
	bus_write_4(sc->sc_mem_res, off, val);
}

static device_t
bytgpio_get_bus(device_t dev)
{
	struct bytgpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->sc_busdev);
}

static int
bytgpio_pin_max(device_t dev, int *maxpin)
{
	struct bytgpio_softc *sc;

	sc = device_get_softc(dev);

	*maxpin = sc->sc_npins - 1;

	return (0);
}

static int
bytgpio_valid_pin(struct bytgpio_softc *sc, int pin)
{

	if (pin >= sc->sc_npins || sc->sc_mem_res == NULL)
		return (EINVAL);

	return (0);
}

/*
 * Returns true if pad configured to be used as GPIO
 */
static bool
bytgpio_pad_is_gpio(struct bytgpio_softc *sc, int pin)
{
	if ((sc->sc_pinpad_map[pin].pad_func == PADCONF_FUNC_ANY) ||
	    (sc->sc_pad_funcs[pin] == sc->sc_pinpad_map[pin].pad_func))
		return (true);
	else
		return (false);
}

static int
bytgpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct bytgpio_softc *sc;

	sc = device_get_softc(dev);
	if (bytgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	*caps = 0;
	if (bytgpio_pad_is_gpio(sc, pin))
		*caps = GPIO_PIN_INPUT | GPIO_PIN_OUTPUT;

	return (0);
}

static int
bytgpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct bytgpio_softc *sc;
	uint32_t reg, val;

	sc = device_get_softc(dev);
	if (bytgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	*flags = 0;
	if (!bytgpio_pad_is_gpio(sc, pin))
		return (0);

	/* Get the current pin state */
	BYTGPIO_LOCK(sc);
	reg = BYGPIO_PIN_REGISTER(sc, pin, BYTGPIO_PAD_VAL);
	val = bytgpio_read_4(sc, reg);
	if ((val & BYTGPIO_PAD_VAL_I_OUTPUT_ENABLED) == 0)
		*flags |= GPIO_PIN_OUTPUT;
	/*
	 * this bit can be cleared to read current output value
	 * sou output bit takes precedense
	 */
	else if ((val & BYTGPIO_PAD_VAL_I_INPUT_ENABLED) == 0)
		*flags |= GPIO_PIN_INPUT;
	BYTGPIO_UNLOCK(sc);

	return (0);
}

static int
bytgpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct bytgpio_softc *sc;
	uint32_t reg, val;
	uint32_t allowed;

	sc = device_get_softc(dev);
	if (bytgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	if (bytgpio_pad_is_gpio(sc, pin))
		allowed = GPIO_PIN_INPUT | GPIO_PIN_OUTPUT;
	else
		allowed = 0;

	/* 
	 * Only directtion flag allowed
	 */
	if (flags & ~allowed)
		return (EINVAL);

	/* 
	 * Not both directions simultaneously
	 */
	if ((flags & allowed) == allowed)
		return (EINVAL);

	/* Set the GPIO mode and state */
	BYTGPIO_LOCK(sc);
	reg = BYGPIO_PIN_REGISTER(sc, pin, BYTGPIO_PAD_VAL);
	val = bytgpio_read_4(sc, reg);
	val = val | BYTGPIO_PAD_VAL_DIR_MASK;
	if (flags & GPIO_PIN_INPUT)
		val = val & ~BYTGPIO_PAD_VAL_I_INPUT_ENABLED;
	if (flags & GPIO_PIN_OUTPUT)
		val = val & ~BYTGPIO_PAD_VAL_I_OUTPUT_ENABLED;
	bytgpio_write_4(sc, reg, val);
	BYTGPIO_UNLOCK(sc);

	return (0);
}

static int
bytgpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct bytgpio_softc *sc;

	sc = device_get_softc(dev);
	if (bytgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	/* Set a very simple name */
	snprintf(name, GPIOMAXNAME, "%s%u", sc->sc_bank_prefix, pin);
	name[GPIOMAXNAME - 1] = '\0';

	return (0);
}

static int
bytgpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct bytgpio_softc *sc;
	uint32_t reg, val;

	sc = device_get_softc(dev);
	if (bytgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	if (!bytgpio_pad_is_gpio(sc, pin))
		return (EINVAL);

	BYTGPIO_LOCK(sc);
	reg = BYGPIO_PIN_REGISTER(sc, pin, BYTGPIO_PAD_VAL);
	val = bytgpio_read_4(sc, reg);
	if (value == GPIO_PIN_LOW)
		val = val & ~BYTGPIO_PAD_VAL_LEVEL;
	else
		val = val | BYTGPIO_PAD_VAL_LEVEL;
	bytgpio_write_4(sc, reg, val);
	BYTGPIO_UNLOCK(sc);

	return (0);
}

static int
bytgpio_pin_get(device_t dev, uint32_t pin, unsigned int *value)
{
	struct bytgpio_softc *sc;
	uint32_t reg, val;

	sc = device_get_softc(dev);
	if (bytgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);
	/*
	 * Report non-GPIO pads as pin LOW
	 */
	if (!bytgpio_pad_is_gpio(sc, pin)) {
		*value = GPIO_PIN_LOW;
		return (0);
	}

	BYTGPIO_LOCK(sc);
	reg = BYGPIO_PIN_REGISTER(sc, pin, BYTGPIO_PAD_VAL);
	/*
	 * And read actual value
	 */
	val = bytgpio_read_4(sc, reg);
	if (val & BYTGPIO_PAD_VAL_LEVEL)
		*value = GPIO_PIN_HIGH;
	else
		*value = GPIO_PIN_LOW;
	BYTGPIO_UNLOCK(sc);

	return (0);
}

static int
bytgpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct bytgpio_softc *sc;
	uint32_t reg, val;

	sc = device_get_softc(dev);
	if (bytgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	if (!bytgpio_pad_is_gpio(sc, pin))
		return (EINVAL);

	/* Toggle the pin */
	BYTGPIO_LOCK(sc);
	reg = BYGPIO_PIN_REGISTER(sc, pin, BYTGPIO_PAD_VAL);
	val = bytgpio_read_4(sc, reg);
	val = val ^ BYTGPIO_PAD_VAL_LEVEL;
	bytgpio_write_4(sc, reg, val);
	BYTGPIO_UNLOCK(sc);

	return (0);
}

static int
bytgpio_probe(device_t dev)
{
	static char *gpio_ids[] = { "INT33FC", NULL };
	int rv;

	if (acpi_disabled("gpio"))
		return (ENXIO);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, gpio_ids, NULL);
	if (rv <= 0)
		device_set_desc(dev, "Intel Baytrail GPIO Controller");
	return (rv);
}

static int
bytgpio_attach(device_t dev)
{
	struct bytgpio_softc	*sc;
	ACPI_STATUS status;
	int uid;
	int pin;
	uint32_t reg, val;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_handle = acpi_get_handle(dev);
	status = acpi_GetInteger(sc->sc_handle, "_UID", &uid);
	if (ACPI_FAILURE(status)) {
		device_printf(dev, "failed to read _UID\n");
		return (ENXIO);
	}

	BYTGPIO_LOCK_INIT(sc);

	switch (uid) {
	case SCORE_UID:
		sc->sc_npins = SCORE_PINS;
		sc->sc_bank_prefix = SCORE_BANK_PREFIX;
		sc->sc_pinpad_map = bytgpio_score_pins;
		break;
	case NCORE_UID:
		sc->sc_npins = NCORE_PINS;
		sc->sc_bank_prefix = NCORE_BANK_PREFIX;
		sc->sc_pinpad_map = bytgpio_ncore_pins;
		break;
	case SUS_UID:
		sc->sc_npins = SUS_PINS;
		sc->sc_bank_prefix = SUS_BANK_PREFIX;
		sc->sc_pinpad_map = bytgpio_sus_pins;
		break;
	default:
		device_printf(dev, "invalid _UID value: %d\n", uid);
		goto error;
	}

	sc->sc_pad_funcs = malloc(sizeof(int)*sc->sc_npins, M_DEVBUF,
	    M_WAITOK | M_ZERO);

	sc->sc_mem_rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(sc->sc_dev,
	    SYS_RES_MEMORY, &sc->sc_mem_rid, RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "can't allocate resource\n");
		goto error;
	}

	for (pin = 0; pin < sc->sc_npins; pin++) {
	    reg = BYGPIO_PIN_REGISTER(sc, pin, BYTGPIO_PCONF0);
	    val = bytgpio_read_4(sc, reg);
	    sc->sc_pad_funcs[pin] = val & BYTGPIO_PCONF0_FUNC_MASK;
	}

	sc->sc_busdev = gpiobus_attach_bus(dev);
	if (sc->sc_busdev == NULL) {
		BYTGPIO_LOCK_DESTROY(sc);
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->sc_mem_rid, sc->sc_mem_res);
		return (ENXIO);
	}

	return (0);

error:
	BYTGPIO_LOCK_DESTROY(sc);

	return (ENXIO);
}


static int
bytgpio_detach(device_t dev)
{
	struct bytgpio_softc	*sc;

	sc = device_get_softc(dev);

	if (sc->sc_busdev)
		gpiobus_detach_bus(dev);

	BYTGPIO_LOCK_DESTROY(sc);

	if (sc->sc_pad_funcs)
		free(sc->sc_pad_funcs, M_DEVBUF);

	if (sc->sc_mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->sc_mem_rid, sc->sc_mem_res);

	return (0);
}

static device_method_t bytgpio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, bytgpio_probe),
	DEVMETHOD(device_attach, bytgpio_attach),
	DEVMETHOD(device_detach, bytgpio_detach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus, bytgpio_get_bus),
	DEVMETHOD(gpio_pin_max, bytgpio_pin_max),
	DEVMETHOD(gpio_pin_getname, bytgpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags, bytgpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps, bytgpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags, bytgpio_pin_setflags),
	DEVMETHOD(gpio_pin_get, bytgpio_pin_get),
	DEVMETHOD(gpio_pin_set, bytgpio_pin_set),
	DEVMETHOD(gpio_pin_toggle, bytgpio_pin_toggle),

	DEVMETHOD_END
};

static driver_t bytgpio_driver = {
	"gpio",
	bytgpio_methods,
	sizeof(struct bytgpio_softc),
};

static devclass_t bytgpio_devclass;
DRIVER_MODULE(bytgpio, acpi, bytgpio_driver, bytgpio_devclass, 0, 0);
MODULE_DEPEND(bytgpio, acpi, 1, 1, 1);
MODULE_DEPEND(bytgpio, gpiobus, 1, 1, 1);
