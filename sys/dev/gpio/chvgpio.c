/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Tom Jones <tj@enoti.me>
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
 * Copyright (c) 2016 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/rman.h>
#include <sys/types.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>
#include <dev/gpio/gpiobusvar.h>

#include "opt_platform.h"
#include "opt_acpi.h"
#include "gpio_if.h"

#include "chvgpio_reg.h"

/*
 *     Macros for driver mutex locking
 */
#define CHVGPIO_LOCK(_sc)               mtx_lock_spin(&(_sc)->sc_mtx)
#define CHVGPIO_UNLOCK(_sc)             mtx_unlock_spin(&(_sc)->sc_mtx)
#define CHVGPIO_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit((_sc)->sc_dev), \
	"chvgpio", MTX_SPIN)
#define CHVGPIO_LOCK_DESTROY(_sc)       mtx_destroy(&(_sc)->sc_mtx)
#define CHVGPIO_ASSERT_LOCKED(_sc)      mtx_assert(&(_sc)->sc_mtx, MA_OWNED)
#define CHVGPIO_ASSERT_UNLOCKED(_sc) 	mtx_assert(&(_sc)->sc_mtx, MA_NOTOWNED)

struct chvgpio_softc {
	device_t 	sc_dev;
	device_t 	sc_busdev;
	struct mtx 	sc_mtx;

	ACPI_HANDLE	sc_handle;

	int		sc_mem_rid;
	struct resource *sc_mem_res;

	int		sc_irq_rid;
	struct resource *sc_irq_res;
	void		*intr_handle;

	const char	*sc_bank_prefix;
	const int  	*sc_pins;
	int 		sc_npins;
	int 		sc_ngroups;
	const char **sc_pin_names;
};

static void chvgpio_intr(void *);
static int chvgpio_probe(device_t);
static int chvgpio_attach(device_t);
static int chvgpio_detach(device_t);

static inline int
chvgpio_pad_cfg0_offset(int pin)
{
	return (CHVGPIO_PAD_CFG0 + 1024 * (pin / 15) + 8 * (pin % 15));
}

static inline int
chvgpio_read_pad_cfg0(struct chvgpio_softc *sc, int pin)
{
	return bus_read_4(sc->sc_mem_res, chvgpio_pad_cfg0_offset(pin));
}

static inline void
chvgpio_write_pad_cfg0(struct chvgpio_softc *sc, int pin, uint32_t val)
{
	bus_write_4(sc->sc_mem_res, chvgpio_pad_cfg0_offset(pin), val);
}

static inline int
chvgpio_read_pad_cfg1(struct chvgpio_softc *sc, int pin)
{
	return bus_read_4(sc->sc_mem_res, chvgpio_pad_cfg0_offset(pin) + 4);
}

static device_t
chvgpio_get_bus(device_t dev)
{
	struct chvgpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->sc_busdev);
}

static int
chvgpio_pin_max(device_t dev, int *maxpin)
{
	struct chvgpio_softc *sc;

	sc = device_get_softc(dev);

	*maxpin = sc->sc_npins - 1;

	return (0);
}

static int
chvgpio_valid_pin(struct chvgpio_softc *sc, int pin)
{
	if (pin < 0)
		return EINVAL;
	if ((pin / 15) >= sc->sc_ngroups)
		return EINVAL;
	if ((pin % 15) >= sc->sc_pins[pin / 15])
		return EINVAL;
	return (0);
}

static int
chvgpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct chvgpio_softc *sc;

	sc = device_get_softc(dev);
	if (chvgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	/* return pin name from datasheet */
	snprintf(name, GPIOMAXNAME, "%s", sc->sc_pin_names[pin]);
	name[GPIOMAXNAME - 1] = '\0';
	return (0);
}

static int
chvgpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct chvgpio_softc *sc;

	sc = device_get_softc(dev);
	if (chvgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	*caps = 0;
	if (chvgpio_valid_pin(sc, pin))
		*caps = GPIO_PIN_INPUT | GPIO_PIN_OUTPUT;

	return (0);
}

static int
chvgpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct chvgpio_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);
	if (chvgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	*flags = 0;

	/* Get the current pin state */
	CHVGPIO_LOCK(sc);
	val = chvgpio_read_pad_cfg0(sc, pin);

	if (val & CHVGPIO_PAD_CFG0_GPIOCFG_GPIO ||
		val & CHVGPIO_PAD_CFG0_GPIOCFG_GPO)
		*flags |= GPIO_PIN_OUTPUT;

	if (val & CHVGPIO_PAD_CFG0_GPIOCFG_GPIO ||
		val & CHVGPIO_PAD_CFG0_GPIOCFG_GPI)
		*flags |= GPIO_PIN_INPUT;

	val = chvgpio_read_pad_cfg1(sc, pin);

	CHVGPIO_UNLOCK(sc);
	return (0);
}

static int
chvgpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct chvgpio_softc *sc;
	uint32_t val;
	uint32_t allowed;

	sc = device_get_softc(dev);
	if (chvgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	allowed = GPIO_PIN_INPUT | GPIO_PIN_OUTPUT;

	/*
	 * Only direction flag allowed
	 */
	if (flags & ~allowed)
		return (EINVAL);

	/*
	 * Not both directions simultaneously
	 */
	if ((flags & allowed) == allowed)
		return (EINVAL);

	/* Set the GPIO mode and state */
	CHVGPIO_LOCK(sc);
	val = chvgpio_read_pad_cfg0(sc, pin);
	if (flags & GPIO_PIN_INPUT)
		val = val & CHVGPIO_PAD_CFG0_GPIOCFG_GPI;
	if (flags & GPIO_PIN_OUTPUT)
		val = val & CHVGPIO_PAD_CFG0_GPIOCFG_GPO;
	chvgpio_write_pad_cfg0(sc, pin, val);
	CHVGPIO_UNLOCK(sc);

	return (0);
}

static int
chvgpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct chvgpio_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);
	if (chvgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	CHVGPIO_LOCK(sc);
	val = chvgpio_read_pad_cfg0(sc, pin);
	if (value == GPIO_PIN_LOW)
		val = val & ~CHVGPIO_PAD_CFG0_GPIOTXSTATE;
	else
		val = val | CHVGPIO_PAD_CFG0_GPIOTXSTATE;
	chvgpio_write_pad_cfg0(sc, pin, val);
	CHVGPIO_UNLOCK(sc);

	return (0);
}

static int
chvgpio_pin_get(device_t dev, uint32_t pin, unsigned int *value)
{
	struct chvgpio_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);
	if (chvgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	CHVGPIO_LOCK(sc);

	/* Read pin value */
	val = chvgpio_read_pad_cfg0(sc, pin);
	if (val & CHVGPIO_PAD_CFG0_GPIORXSTATE)
		*value = GPIO_PIN_HIGH;
	else
		*value = GPIO_PIN_LOW;

	CHVGPIO_UNLOCK(sc);

	return (0);
}

static int
chvgpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct chvgpio_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);
	if (chvgpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	CHVGPIO_LOCK(sc);

	/* Toggle the pin */
	val = chvgpio_read_pad_cfg0(sc, pin);
	val = val ^ CHVGPIO_PAD_CFG0_GPIOTXSTATE;
	chvgpio_write_pad_cfg0(sc, pin, val);

	CHVGPIO_UNLOCK(sc);

	return (0);
}

static char *chvgpio_hids[] = {
	"INT33FF",
	NULL
};

static int
chvgpio_probe(device_t dev)
{
    int rv;
    
    if (acpi_disabled("chvgpio"))
        return (ENXIO);
    rv = ACPI_ID_PROBE(device_get_parent(dev), dev, chvgpio_hids, NULL);
    if (rv <= 0)
	device_set_desc(dev, "Intel Cherry View GPIO");
    return (rv);
}

static int
chvgpio_attach(device_t dev)
{
	struct chvgpio_softc *sc;
	ACPI_STATUS status;
	int uid;
	int i;
	int error;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_handle = acpi_get_handle(dev);

	status = acpi_GetInteger(sc->sc_handle, "_UID", &uid);
	if (ACPI_FAILURE(status)) {
		device_printf(dev, "failed to read _UID\n");
		return (ENXIO);
	}

	CHVGPIO_LOCK_INIT(sc);

	switch (uid) {
	case SW_UID:
		sc->sc_bank_prefix = SW_BANK_PREFIX;
		sc->sc_pins = chv_southwest_pins;
		sc->sc_pin_names = chv_southwest_pin_names;
		break;
	case N_UID:
		sc->sc_bank_prefix = N_BANK_PREFIX;
		sc->sc_pins = chv_north_pins;
		sc->sc_pin_names = chv_north_pin_names;
		break;
	case E_UID:
		sc->sc_bank_prefix = E_BANK_PREFIX;
		sc->sc_pins = chv_east_pins;
		sc->sc_pin_names = chv_east_pin_names;
		break;
	case SE_UID:
		sc->sc_bank_prefix = SE_BANK_PREFIX;
		sc->sc_pins = chv_southeast_pins;
		sc->sc_pin_names = chv_southeast_pin_names;
		break;
	default:
		device_printf(dev, "invalid _UID value: %d\n", uid);
		return (ENXIO);
	}

	for (i = 0; sc->sc_pins[i] >= 0; i++) {
		sc->sc_npins += sc->sc_pins[i];
		sc->sc_ngroups++;
	}

	sc->sc_mem_rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(sc->sc_dev, SYS_RES_MEMORY,
		&sc->sc_mem_rid, RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		CHVGPIO_LOCK_DESTROY(sc);
		device_printf(dev, "can't allocate memory resource\n");
		return (ENOMEM);
	}

	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
		&sc->sc_irq_rid, RF_ACTIVE);

	if (!sc->sc_irq_res) {
		CHVGPIO_LOCK_DESTROY(sc);
		bus_release_resource(dev, SYS_RES_MEMORY,
			sc->sc_mem_rid, sc->sc_mem_res);
		device_printf(dev, "can't allocate irq resource\n");
		return (ENOMEM);
	}

	error = bus_setup_intr(sc->sc_dev, sc->sc_irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
		NULL, chvgpio_intr, sc, &sc->intr_handle);


	if (error) {
		device_printf(sc->sc_dev, "unable to setup irq: error %d\n", error);
		CHVGPIO_LOCK_DESTROY(sc);
		bus_release_resource(dev, SYS_RES_MEMORY,
			sc->sc_mem_rid, sc->sc_mem_res);
		bus_release_resource(dev, SYS_RES_IRQ,
			sc->sc_irq_rid, sc->sc_irq_res);
		return (ENXIO);
	}

	/* Mask and ack all interrupts. */
	bus_write_4(sc->sc_mem_res, CHVGPIO_INTERRUPT_MASK, 0);
	bus_write_4(sc->sc_mem_res, CHVGPIO_INTERRUPT_STATUS, 0xffff);

	sc->sc_busdev = gpiobus_attach_bus(dev);
	if (sc->sc_busdev == NULL) {
		CHVGPIO_LOCK_DESTROY(sc);
		bus_release_resource(dev, SYS_RES_MEMORY,
			sc->sc_mem_rid, sc->sc_mem_res);
		bus_release_resource(dev, SYS_RES_IRQ,
			sc->sc_irq_rid, sc->sc_irq_res);
		return (ENXIO);
	}

	return (0);
}

static void
chvgpio_intr(void *arg)
{
	struct chvgpio_softc *sc = arg;
	uint32_t reg;
	int line;

	reg = bus_read_4(sc->sc_mem_res, CHVGPIO_INTERRUPT_STATUS);
	for (line = 0; line < 16; line++) {
		if ((reg & (1 << line)) == 0)
			continue;
		bus_write_4(sc->sc_mem_res, CHVGPIO_INTERRUPT_STATUS, 1 << line);
	}
}

static int
chvgpio_detach(device_t dev)
{
	struct chvgpio_softc *sc;
	sc = device_get_softc(dev);

	if (sc->sc_busdev)
		gpiobus_detach_bus(dev);

	if (sc->intr_handle != NULL)
	    bus_teardown_intr(sc->sc_dev, sc->sc_irq_res, sc->intr_handle);
	if (sc->sc_irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid, sc->sc_irq_res);
	if (sc->sc_mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_mem_rid, sc->sc_mem_res);

	CHVGPIO_LOCK_DESTROY(sc);

    return (0);
}

static device_method_t chvgpio_methods[] = {
	DEVMETHOD(device_probe,     	chvgpio_probe),
	DEVMETHOD(device_attach,    	chvgpio_attach),
	DEVMETHOD(device_detach,    	chvgpio_detach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus, 	chvgpio_get_bus),
	DEVMETHOD(gpio_pin_max, 	chvgpio_pin_max),
	DEVMETHOD(gpio_pin_getname, 	chvgpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags,	chvgpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps, 	chvgpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags,	chvgpio_pin_setflags),
	DEVMETHOD(gpio_pin_get, 	chvgpio_pin_get),
	DEVMETHOD(gpio_pin_set, 	chvgpio_pin_set),
	DEVMETHOD(gpio_pin_toggle, 	chvgpio_pin_toggle),

	DEVMETHOD_END
};

static driver_t chvgpio_driver = {
    .name = "gpio",
    .methods = chvgpio_methods,
    .size = sizeof(struct chvgpio_softc)
};

static devclass_t chvgpio_devclass;
DRIVER_MODULE(chvgpio, acpi, chvgpio_driver, chvgpio_devclass, NULL , NULL);
MODULE_DEPEND(chvgpio, acpi, 1, 1, 1);
MODULE_DEPEND(chvgpio, gpiobus, 1, 1, 1);

MODULE_VERSION(chvgpio, 1);
