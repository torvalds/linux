/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Landon Fuller under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/limits.h>
#include <sys/module.h>

#include <machine/_inttypes.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>
#include <dev/gpio/gpiobusvar.h>

#include "gpio_if.h"

#include "bhnd_nvram_map.h"

#include "chipcreg.h"
#include "chipc_gpiovar.h"

/*
 * ChipCommon GPIO driver
 */

static int			chipc_gpio_check_flags(
				    struct chipc_gpio_softc *sc,
				    uint32_t pin_num, uint32_t flags,
				    chipc_gpio_pin_mode *mode);
static int			chipc_gpio_pin_update(
				    struct chipc_gpio_softc *sc,
				    struct chipc_gpio_update *update,
				    uint32_t pin_num, uint32_t flags);
static int			chipc_gpio_commit_update(
				    struct chipc_gpio_softc *sc,
				    struct chipc_gpio_update *update);
static chipc_gpio_pin_mode	chipc_gpio_pin_get_mode(
				    struct chipc_gpio_softc *sc,
				    uint32_t pin_num);


/* Debugging flags */
static u_long chipc_gpio_debug = 0;
TUNABLE_ULONG("hw.bhnd_chipc.gpio_debug", &chipc_gpio_debug);

enum {
	/** Allow userspace GPIO access on bridged network (e.g. wi-fi)
	  * adapters */
	CC_GPIO_DEBUG_ADAPTER_GPIOC = 1 << 0,
};

#define	CC_GPIO_DEBUG(_type)	(CC_GPIO_DEBUG_ ## _type & chipc_gpio_debug)

static struct bhnd_device_quirk chipc_gpio_quirks[];

/* Supported parent core device identifiers */
static const struct bhnd_device chipc_gpio_devices[] = {
	BHND_DEVICE(BCM, CC, "Broadcom ChipCommon GPIO", chipc_gpio_quirks),
	BHND_DEVICE_END
};

/* Device quirks table */
static struct bhnd_device_quirk chipc_gpio_quirks[] = {
	BHND_CORE_QUIRK	(HWREV_LTE(10),	CC_GPIO_QUIRK_NO_EVENTS),
	BHND_CORE_QUIRK	(HWREV_LTE(15),	CC_GPIO_QUIRK_NO_DCTIMER),
	BHND_CORE_QUIRK	(HWREV_LTE(19),	CC_GPIO_QUIRK_NO_PULLUPDOWN),

	BHND_DEVICE_QUIRK_END
};

static int
chipc_gpio_probe(device_t dev)
{
	const struct bhnd_device	*id;
	device_t			 chipc;

	/* Look for compatible chipc parent */
	chipc = device_get_parent(dev);
	id = bhnd_device_lookup(chipc, chipc_gpio_devices,
	    sizeof(chipc_gpio_devices[0]));
	if (id == NULL)
		return (ENXIO);

	device_set_desc(dev, id->desc);
	return (BUS_PROBE_NOWILDCARD);
}

static int
chipc_gpio_attach(device_t dev)
{
	struct chipc_gpio_softc	*sc;
	device_t		 chipc;
	int			 error;

	chipc = device_get_parent(dev);

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->quirks = bhnd_device_quirks(chipc, chipc_gpio_devices,
	    sizeof(chipc_gpio_devices[0]));

	/* If this is a bridged wi-fi adapter, we don't want to support
	 * userspace requests via gpioc(4) */
	if (bhnd_get_attach_type(chipc) == BHND_ATTACH_ADAPTER) {
		if (!CC_GPIO_DEBUG(ADAPTER_GPIOC))
			sc->quirks |= CC_GPIO_QUIRK_NO_GPIOC;
	}

	CC_GPIO_LOCK_INIT(sc);

	sc->mem_rid = 0;
	sc->mem_res = bhnd_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->mem_rid,
	    RF_ACTIVE|RF_SHAREABLE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "failed to allocate chipcommon registers\n");
		error = ENXIO;
		goto failed;
	}

	/*
	 * If hardware 'pulsate' support is available, set the timer duty-cycle
	 * to either the NVRAM 'leddc' value if available, or the default duty
	 * cycle.
	 */
	if (!CC_GPIO_QUIRK(sc, NO_DCTIMER)) {
		uint32_t dctimerval;

		error = bhnd_nvram_getvar_uint32(chipc, BHND_NVAR_LEDDC,
		    &dctimerval);
		if (error == ENOENT) {
			/* Fall back on default duty cycle */
			dctimerval = CHIPC_GPIOTIMERVAL_DEFAULT;
		} else if (error) {
			device_printf(dev, "error reading %s from NVRAM: %d\n",
			    BHND_NVAR_LEDDC, error);
			goto failed;
		}

		CC_GPIO_WR4(sc, CHIPC_GPIOTIMERVAL, dctimerval);
	}

	/* Attach gpioc/gpiobus */
	if (CC_GPIO_QUIRK(sc, NO_GPIOC)) {
		sc->gpiobus = NULL;
	} else {
		if ((sc->gpiobus = gpiobus_attach_bus(dev)) == NULL) {
			device_printf(dev, "failed to attach gpiobus\n");
			error = ENXIO;
			goto failed;
		}
	}

	/* Register as the bus GPIO provider */
	if ((error = bhnd_register_provider(dev, BHND_SERVICE_GPIO))) {
		device_printf(dev, "failed to register gpio with bus: %d\n",
		    error);
		goto failed;
	}

	return (0);

failed:
	device_delete_children(dev);

	if (sc->mem_res != NULL) {
		bhnd_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid,
		    sc->mem_res);
	}

	CC_GPIO_LOCK_DESTROY(sc);

	return (error);
}

static int
chipc_gpio_detach(device_t dev)
{
	struct chipc_gpio_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	if ((error = bus_generic_detach(dev)))
		return (error);

	if ((error = bhnd_deregister_provider(dev, BHND_SERVICE_ANY)))
		return (error);

	bhnd_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
	CC_GPIO_LOCK_DESTROY(sc);

	return (0);
}

static device_t
chipc_gpio_get_bus(device_t dev)
{
	struct chipc_gpio_softc *sc = device_get_softc(dev);

	return (sc->gpiobus);
}

static int
chipc_gpio_pin_max(device_t dev, int *maxpin)
{
	*maxpin = CC_GPIO_NPINS-1;
	return (0);
}

static int
chipc_gpio_pin_set(device_t dev, uint32_t pin_num, uint32_t pin_value)
{
	struct chipc_gpio_softc	*sc;
	bool			 pin_high;
	int			 error;

	sc = device_get_softc(dev);
	error = 0;

	if (!CC_GPIO_VALID_PIN(pin_num))
		return (EINVAL);

	switch (pin_value) {
	case GPIO_PIN_HIGH:
		pin_high = true;
		break;
	case GPIO_PIN_LOW:
		pin_high = false;
		break;
	default:
		return (EINVAL);
	}

	CC_GPIO_LOCK(sc);

	switch (chipc_gpio_pin_get_mode(sc, pin_num)) {
	case CC_GPIO_PIN_INPUT:
	case CC_GPIO_PIN_TRISTATE:
		error = ENODEV;
		break;

	case CC_GPIO_PIN_OUTPUT:
		CC_GPIO_WRFLAG(sc, pin_num, GPIOOUT, pin_high);
		break;
	}

	CC_GPIO_UNLOCK(sc);

	return (error);
}

static int
chipc_gpio_pin_get(device_t dev, uint32_t pin_num, uint32_t *pin_value)
{
	struct chipc_gpio_softc	*sc;
	bool			 pin_high;

	if (!CC_GPIO_VALID_PIN(pin_num))
		return (EINVAL);

	sc = device_get_softc(dev);
	pin_high = false;

	CC_GPIO_LOCK(sc);

	switch (chipc_gpio_pin_get_mode(sc, pin_num)) {
	case CC_GPIO_PIN_INPUT:
		pin_high = CC_GPIO_RDFLAG(sc, pin_num, GPIOIN);
		break;

	case CC_GPIO_PIN_OUTPUT:
		pin_high = CC_GPIO_RDFLAG(sc, pin_num, GPIOOUT);
		break;

	case CC_GPIO_PIN_TRISTATE:
		pin_high = false;
		break;
	}

	CC_GPIO_UNLOCK(sc);

	*pin_value = pin_high ? GPIO_PIN_HIGH : GPIO_PIN_LOW;

	return (0);
}

static int
chipc_gpio_pin_toggle(device_t dev, uint32_t pin_num)
{
	struct chipc_gpio_softc	*sc;
	bool			 pin_high;
	int			 error;

	if (!CC_GPIO_VALID_PIN(pin_num))
		return (EINVAL);

	sc = device_get_softc(dev);
	error = 0;

	CC_GPIO_LOCK(sc);

	switch (chipc_gpio_pin_get_mode(sc, pin_num)) {
	case CC_GPIO_PIN_INPUT:
	case CC_GPIO_PIN_TRISTATE:
		error = ENODEV;
		break;

	case CC_GPIO_PIN_OUTPUT:
		pin_high = CC_GPIO_RDFLAG(sc, pin_num, GPIOOUT);
		CC_GPIO_WRFLAG(sc, pin_num, GPIOOUT, !pin_high);
		break;
	}

	CC_GPIO_UNLOCK(sc);

	return (error);
}

static int
chipc_gpio_pin_getcaps(device_t dev, uint32_t pin_num, uint32_t *caps)
{
	struct chipc_gpio_softc	*sc = device_get_softc(dev);

	if (!CC_GPIO_VALID_PIN(pin_num))
		return (EINVAL);

	*caps = (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_TRISTATE);

	if (!CC_GPIO_QUIRK(sc, NO_PULLUPDOWN))
		*caps |= (GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN);

	if (!CC_GPIO_QUIRK(sc, NO_DCTIMER))
		*caps |= GPIO_PIN_PULSATE;

	return (0);
}

static int
chipc_gpio_pin_getflags(device_t dev, uint32_t pin_num, uint32_t *flags)
{
	struct chipc_gpio_softc *sc = device_get_softc(dev);

	if (!CC_GPIO_VALID_PIN(pin_num))
		return (EINVAL);

	CC_GPIO_LOCK(sc);

	switch (chipc_gpio_pin_get_mode(sc, pin_num)) {
	case CC_GPIO_PIN_INPUT:
		*flags = GPIO_PIN_INPUT;

		if (!CC_GPIO_QUIRK(sc, NO_PULLUPDOWN)) {
			if (CC_GPIO_RDFLAG(sc, pin_num, GPIOPU)) {
				*flags |= GPIO_PIN_PULLUP;
			} else if (CC_GPIO_RDFLAG(sc, pin_num, GPIOPD)) {
				*flags |= GPIO_PIN_PULLDOWN;
			}
		}
		break;

	case CC_GPIO_PIN_OUTPUT:
		*flags = GPIO_PIN_OUTPUT;

		if (!CC_GPIO_QUIRK(sc, NO_DCTIMER)) {
			if (CC_GPIO_RDFLAG(sc, pin_num, GPIOTIMEROUTMASK))
				*flags |= GPIO_PIN_PULSATE;
		}

		break;

	case CC_GPIO_PIN_TRISTATE:
		*flags = GPIO_PIN_TRISTATE|GPIO_PIN_OUTPUT;
		break;
	}

	CC_GPIO_UNLOCK(sc);

	return (0);
}

static int
chipc_gpio_pin_getname(device_t dev, uint32_t pin_num, char *name)
{
	int ret;

	if (!CC_GPIO_VALID_PIN(pin_num))
		return (EINVAL);

	ret = snprintf(name, GPIOMAXNAME, "bhnd_gpio%02" PRIu32, pin_num);

	if (ret < 0)
		return (ENXIO);

	if (ret >= GPIOMAXNAME)
		return (ENOMEM);
	
	return (0);
}

static int
chipc_gpio_pin_setflags(device_t dev, uint32_t pin_num, uint32_t flags)
{
	struct chipc_gpio_softc		*sc;
	struct chipc_gpio_update	 upd;
	int				 error;
	
	sc = device_get_softc(dev);

	if (!CC_GPIO_VALID_PIN(pin_num))
		return (EINVAL);

	/* Produce an update descriptor */
	memset(&upd, 0, sizeof(upd));
	if ((error = chipc_gpio_pin_update(sc, &upd, pin_num, flags)))
		return (error);

	/* Commit the update */
	CC_GPIO_LOCK(sc);
	error = chipc_gpio_commit_update(sc, &upd);
	CC_GPIO_UNLOCK(sc);

	return (error);
}

static int
chipc_gpio_pin_access_32(device_t dev, uint32_t first_pin, uint32_t clear_pins,
    uint32_t change_pins, uint32_t *orig_pins)
{
	struct chipc_gpio_softc		*sc;
	struct chipc_gpio_update	 upd;
	uint32_t			 out, outen, ctrl;
	uint32_t			 num_pins;
	int				 error;

	sc = device_get_softc(dev);

	if (first_pin >= CC_GPIO_NPINS)
		return (EINVAL);

	/* Determine the actual number of referenced pins */
	if (clear_pins == 0 && change_pins == 0) {
		num_pins = CC_GPIO_NPINS - first_pin;
	} else {
		int num_clear_pins, num_change_pins;

		num_clear_pins = flsl((u_long)clear_pins);
		num_change_pins = flsl((u_long)change_pins);
		num_pins = MAX(num_clear_pins, num_change_pins);
	}

	/* Validate the full pin range */
	if (!CC_GPIO_VALID_PINS(first_pin, num_pins))
		return (EINVAL);

	/* Produce an update descriptor for all pins, relative to the current
	 * pin state */
	CC_GPIO_LOCK(sc);
	memset(&upd, 0, sizeof(upd));

	out = CC_GPIO_RD4(sc, CHIPC_GPIOOUT);
	outen = CC_GPIO_RD4(sc, CHIPC_GPIOOUTEN);
	ctrl = CC_GPIO_RD4(sc, CHIPC_GPIOCTRL);

	for (uint32_t i = 0; i < num_pins; i++) {
		uint32_t	pin;
		bool		pin_high;

		pin = first_pin + i;

		/* The pin must be configured for output */
		if ((outen & (1 << pin)) == 0) {
			CC_GPIO_UNLOCK(sc);
			return (EINVAL);
		}

		/* The pin must not tristated */
		if ((ctrl & (1 << pin)) != 0) {
			CC_GPIO_UNLOCK(sc);
			return (EINVAL);
		}

		/* Fetch current state */
		if (out & (1 << pin)) {
			pin_high = true;
		} else {
			pin_high = false;
		}

		/* Apply clear/toggle request */
		if (clear_pins & (1 << pin))
			pin_high = false;

		if (change_pins & (1 << pin))
			pin_high = !pin_high;

		/* Add to our update descriptor */
		CC_GPIO_UPDATE(&upd, pin, out, pin_high);
	}

	/* Commit the update */
	error = chipc_gpio_commit_update(sc, &upd);
	CC_GPIO_UNLOCK(sc);

	return (error);
}

static int
chipc_gpio_pin_config_32(device_t dev, uint32_t first_pin, uint32_t num_pins,
    uint32_t *pin_flags)
{
	struct chipc_gpio_softc		*sc;
	struct chipc_gpio_update	 upd;
	int				 error;
	
	sc = device_get_softc(dev);

	if (!CC_GPIO_VALID_PINS(first_pin, num_pins))
		return (EINVAL);

	/* Produce an update descriptor */
	memset(&upd, 0, sizeof(upd));
	for (uint32_t i = 0; i < num_pins; i++) {
		uint32_t pin, flags;

		pin = first_pin + i;
		flags = pin_flags[i];

		/* As per the gpio_config_32 API documentation, any pins for
		 * which neither GPIO_PIN_OUTPUT or GPIO_PIN_INPUT are set
		 * should be ignored and left unmodified */
		if ((flags & (GPIO_PIN_OUTPUT|GPIO_PIN_INPUT)) == 0)
			continue;

		if ((error = chipc_gpio_pin_update(sc, &upd, pin, flags)))
			return (error);
	}

	/* Commit the update */
	CC_GPIO_LOCK(sc);
	error = chipc_gpio_commit_update(sc, &upd);
	CC_GPIO_UNLOCK(sc);

	return (error);
}


/**
 * Commit a single @p reg register update.
 */
static void
chipc_gpio_commit_reg(struct chipc_gpio_softc *sc, bus_size_t offset,
    struct chipc_gpio_reg *reg)
{
	uint32_t value;

	CC_GPIO_LOCK_ASSERT(sc, MA_OWNED);

	if (reg->mask == 0)
		return;

	value = bhnd_bus_read_4(sc->mem_res, offset);
	value &= ~reg->mask;
	value |= reg->value;

	bhnd_bus_write_4(sc->mem_res, offset, value);
}

/**
 * Commit the set of GPIO register updates described by @p update.
 */
static int
chipc_gpio_commit_update(struct chipc_gpio_softc *sc,
    struct chipc_gpio_update *update)
{
	CC_GPIO_LOCK_ASSERT(sc, MA_OWNED);

	/* Commit pulldown/pullup before potentially disabling an output pin */
	chipc_gpio_commit_reg(sc, CHIPC_GPIOPD, &update->pulldown);
	chipc_gpio_commit_reg(sc, CHIPC_GPIOPU, &update->pullup);

	/* Commit output settings before potentially enabling an output pin */
	chipc_gpio_commit_reg(sc, CHIPC_GPIOTIMEROUTMASK,
	    &update->timeroutmask);
	chipc_gpio_commit_reg(sc, CHIPC_GPIOOUT, &update->out);

	/* Commit input/output/tristate modes */
	chipc_gpio_commit_reg(sc, CHIPC_GPIOOUTEN, &update->outen);
	chipc_gpio_commit_reg(sc, CHIPC_GPIOCTRL, &update->ctrl);

	return (0);
}

/**
 * Apply the changes described by @p flags for @p pin_num to the given @p update
 * descriptor.
 */
static int
chipc_gpio_pin_update(struct chipc_gpio_softc *sc,
    struct chipc_gpio_update *update, uint32_t pin_num, uint32_t flags)
{
	chipc_gpio_pin_mode	mode;
	int			error;

	if (!CC_GPIO_VALID_PIN(pin_num))
		return (EINVAL);

	/* Verify flag compatibility and determine the pin mode */
	if ((error = chipc_gpio_check_flags(sc, pin_num, flags, &mode)))
		return (error);

	/* Apply the mode-specific changes */
	switch (mode) {
	case CC_GPIO_PIN_INPUT:
		CC_GPIO_UPDATE(update, pin_num, pullup, false);
		CC_GPIO_UPDATE(update, pin_num, pulldown, false);
		CC_GPIO_UPDATE(update, pin_num, out, false);
		CC_GPIO_UPDATE(update, pin_num, outen, false);
		CC_GPIO_UPDATE(update, pin_num, timeroutmask, false);
		CC_GPIO_UPDATE(update, pin_num, ctrl, false);

		if (flags & GPIO_PIN_PULLUP) {
			CC_GPIO_UPDATE(update, pin_num, pullup, true);
		} else if (flags & GPIO_PIN_PULLDOWN) {
			CC_GPIO_UPDATE(update, pin_num, pulldown, true);
		}

		return (0);

	case CC_GPIO_PIN_OUTPUT:
		CC_GPIO_UPDATE(update, pin_num, pullup, false);
		CC_GPIO_UPDATE(update, pin_num, pulldown, false);
		CC_GPIO_UPDATE(update, pin_num, outen, true);
		CC_GPIO_UPDATE(update, pin_num, timeroutmask, false);
		CC_GPIO_UPDATE(update, pin_num, ctrl, false);

		if (flags & GPIO_PIN_PRESET_HIGH) {
			CC_GPIO_UPDATE(update, pin_num, out, true);
		} else if (flags & GPIO_PIN_PRESET_LOW) {
			CC_GPIO_UPDATE(update, pin_num, out, false);
		}

		if (flags & GPIO_PIN_PULSATE)
			CC_GPIO_UPDATE(update, pin_num, timeroutmask, true);

		return (0);

	case CC_GPIO_PIN_TRISTATE:
		CC_GPIO_UPDATE(update, pin_num, pullup, false);
		CC_GPIO_UPDATE(update, pin_num, pulldown, false);
		CC_GPIO_UPDATE(update, pin_num, out, false);
		CC_GPIO_UPDATE(update, pin_num, outen, false);
		CC_GPIO_UPDATE(update, pin_num, timeroutmask, false);
		CC_GPIO_UPDATE(update, pin_num, ctrl, true);

		if (flags & GPIO_PIN_OUTPUT)
			CC_GPIO_UPDATE(update, pin_num, outen, true);

		return (0);
	}

	device_printf(sc->dev, "unknown pin mode %d\n", mode);
	return (EINVAL);
}

/**
 * Verify that @p flags are valid for use with @p pin_num, and on success,
 * return the pin mode described by @p flags in @p mode.
 * 
 * @param	sc	GPIO driver instance state.
 * @param	pin_num	The pin number to configure.
 * @param	flags	The pin flags to be validated.
 * @param[out]	mode	On success, will be populated with the GPIO pin mode
 *			defined by @p flags.
 * 
 * @retval 0		success
 * @retval EINVAL	if @p flags are invalid.
 */
static int
chipc_gpio_check_flags(struct chipc_gpio_softc *sc, uint32_t pin_num,
    uint32_t flags, chipc_gpio_pin_mode *mode)
{
	uint32_t mode_flag, input_flag, output_flag;

	CC_GPIO_ASSERT_VALID_PIN(sc, pin_num);

	mode_flag = flags & (GPIO_PIN_OUTPUT | GPIO_PIN_INPUT |
	    GPIO_PIN_TRISTATE);
	output_flag = flags & (GPIO_PIN_PRESET_HIGH | GPIO_PIN_PRESET_LOW
	    | GPIO_PIN_PULSATE);
	input_flag = flags & (GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN);

	switch (mode_flag) {
	case GPIO_PIN_OUTPUT:
		/* No input flag(s) should be set */
		if (input_flag != 0)
			return (EINVAL);

		/* Validate our output flag(s) */
		switch (output_flag) {
		case GPIO_PIN_PRESET_HIGH:
		case GPIO_PIN_PRESET_LOW:
		case (GPIO_PIN_PRESET_HIGH|GPIO_PIN_PULSATE):
		case (GPIO_PIN_PRESET_LOW|GPIO_PIN_PULSATE):
		case 0:
			/* Check for unhandled flags */
			if ((flags & ~(mode_flag | output_flag)) != 0)
				return (EINVAL);
	
			*mode = CC_GPIO_PIN_OUTPUT;
			return (0);

		default:
			/* Incompatible output flags */
			return (EINVAL);
		}

	case GPIO_PIN_INPUT:
		/* No output flag(s) should be set */
		if (output_flag != 0)
			return (EINVAL);

		/* Validate our input flag(s) */
		switch (input_flag) {
		case GPIO_PIN_PULLUP:
		case GPIO_PIN_PULLDOWN:
		case 0:
			/* Check for unhandled flags */
			if ((flags & ~(mode_flag | input_flag)) != 0)
				return (EINVAL);

			*mode = CC_GPIO_PIN_INPUT;
			return (0);

		default:
			/* Incompatible input flags */
			return (EINVAL);
		}

		break;

	case (GPIO_PIN_TRISTATE|GPIO_PIN_OUTPUT):
	case GPIO_PIN_TRISTATE:
		/* No input or output flag(s) should be set */
		if (input_flag != 0 || output_flag != 0)
			return (EINVAL);

		/* Check for unhandled flags */
		if ((flags & ~mode_flag) != 0)
			return (EINVAL);

		*mode = CC_GPIO_PIN_TRISTATE;
		return (0);

	default:
		/* Incompatible mode flags */
		return (EINVAL);
	}
}

/**
 * Return the current pin mode for @p pin_num.
 * 
 * @param sc		GPIO driver instance state.
 * @param pin_num	The pin number to query.
 */
static chipc_gpio_pin_mode
chipc_gpio_pin_get_mode(struct chipc_gpio_softc *sc, uint32_t pin_num)
{
	CC_GPIO_LOCK_ASSERT(sc, MA_OWNED);
	CC_GPIO_ASSERT_VALID_PIN(sc, pin_num);

	if (CC_GPIO_RDFLAG(sc, pin_num, GPIOCTRL)) {
		return (CC_GPIO_PIN_TRISTATE);
	} else if (CC_GPIO_RDFLAG(sc, pin_num, GPIOOUTEN)) {
		return (CC_GPIO_PIN_OUTPUT);
	} else {
		return (CC_GPIO_PIN_INPUT);
	}
}

static device_method_t chipc_gpio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		chipc_gpio_probe),
	DEVMETHOD(device_attach,	chipc_gpio_attach),
	DEVMETHOD(device_detach,	chipc_gpio_detach),

	/* GPIO interface */
	DEVMETHOD(gpio_get_bus,		chipc_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		chipc_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	chipc_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags,	chipc_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps,	chipc_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags,	chipc_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get,		chipc_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,		chipc_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,	chipc_gpio_pin_toggle),
	DEVMETHOD(gpio_pin_access_32,	chipc_gpio_pin_access_32),
	DEVMETHOD(gpio_pin_config_32,	chipc_gpio_pin_config_32),

	DEVMETHOD_END
};

static devclass_t gpio_devclass;

DEFINE_CLASS_0(gpio, chipc_gpio_driver, chipc_gpio_methods, sizeof(struct chipc_gpio_softc));
EARLY_DRIVER_MODULE(chipc_gpio, bhnd_chipc, chipc_gpio_driver,
    gpio_devclass, NULL, NULL, BUS_PASS_RESOURCE + BUS_PASS_ORDER_MIDDLE);

MODULE_DEPEND(chipc_gpio, bhnd, 1, 1, 1);
MODULE_DEPEND(chipc_gpio, gpiobus, 1, 1, 1);
MODULE_VERSION(chipc_gpio, 1);
