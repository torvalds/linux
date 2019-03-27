/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#ifdef INTRNG
#include <sys/intr.h>
#endif
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <dev/gpio/gpiobusvar.h>

#include "gpiobus_if.h"

#undef GPIOBUS_DEBUG
#ifdef GPIOBUS_DEBUG
#define	dprintf printf
#else
#define	dprintf(x, arg...)
#endif

static void gpiobus_print_pins(struct gpiobus_ivar *, char *, size_t);
static int gpiobus_parse_pins(struct gpiobus_softc *, device_t, int);
static int gpiobus_probe(device_t);
static int gpiobus_attach(device_t);
static int gpiobus_detach(device_t);
static int gpiobus_suspend(device_t);
static int gpiobus_resume(device_t);
static void gpiobus_probe_nomatch(device_t, device_t);
static int gpiobus_print_child(device_t, device_t);
static int gpiobus_child_location_str(device_t, device_t, char *, size_t);
static int gpiobus_child_pnpinfo_str(device_t, device_t, char *, size_t);
static device_t gpiobus_add_child(device_t, u_int, const char *, int);
static void gpiobus_hinted_child(device_t, const char *, int);

/*
 * GPIOBUS interface
 */
static int gpiobus_acquire_bus(device_t, device_t, int);
static void gpiobus_release_bus(device_t, device_t);
static int gpiobus_pin_setflags(device_t, device_t, uint32_t, uint32_t);
static int gpiobus_pin_getflags(device_t, device_t, uint32_t, uint32_t*);
static int gpiobus_pin_getcaps(device_t, device_t, uint32_t, uint32_t*);
static int gpiobus_pin_set(device_t, device_t, uint32_t, unsigned int);
static int gpiobus_pin_get(device_t, device_t, uint32_t, unsigned int*);
static int gpiobus_pin_toggle(device_t, device_t, uint32_t);

/*
 * XXX -> Move me to better place - gpio_subr.c?
 * Also, this function must be changed when interrupt configuration
 * data will be moved into struct resource.
 */
#ifdef INTRNG

struct resource *
gpio_alloc_intr_resource(device_t consumer_dev, int *rid, u_int alloc_flags,
    gpio_pin_t pin, uint32_t intr_mode)
{
	u_int irq;
	struct intr_map_data_gpio *gpio_data;
	struct resource *res;

	gpio_data = (struct intr_map_data_gpio *)intr_alloc_map_data(
	    INTR_MAP_DATA_GPIO, sizeof(*gpio_data), M_WAITOK | M_ZERO);
	gpio_data->gpio_pin_num = pin->pin;
	gpio_data->gpio_pin_flags = pin->flags;
	gpio_data->gpio_intr_mode = intr_mode;

	irq = intr_map_irq(pin->dev, 0, (struct intr_map_data *)gpio_data);
	res = bus_alloc_resource(consumer_dev, SYS_RES_IRQ, rid, irq, irq, 1,
	    alloc_flags);
	if (res == NULL) {
		intr_free_intr_map_data((struct intr_map_data *)gpio_data);
		return (NULL);
	}
	rman_set_virtual(res, gpio_data);
	return (res);
}
#else
struct resource *
gpio_alloc_intr_resource(device_t consumer_dev, int *rid, u_int alloc_flags,
    gpio_pin_t pin, uint32_t intr_mode)
{

	return (NULL);
}
#endif

int
gpio_check_flags(uint32_t caps, uint32_t flags)
{

	/* Filter unwanted flags. */
	flags &= caps;

	/* Cannot mix input/output together. */
	if (flags & GPIO_PIN_INPUT && flags & GPIO_PIN_OUTPUT)
		return (EINVAL);
	/* Cannot mix pull-up/pull-down together. */
	if (flags & GPIO_PIN_PULLUP && flags & GPIO_PIN_PULLDOWN)
		return (EINVAL);

	return (0);
}

static void
gpiobus_print_pins(struct gpiobus_ivar *devi, char *buf, size_t buflen)
{
	char tmp[128];
	int i, range_start, range_stop, need_coma;

	if (devi->npins == 0)
		return;

	need_coma = 0;
	range_start = range_stop = devi->pins[0];
	for (i = 1; i < devi->npins; i++) {
		if (devi->pins[i] != (range_stop + 1)) {
			if (need_coma)
				strlcat(buf, ",", buflen);
			memset(tmp, 0, sizeof(tmp));
			if (range_start != range_stop)
				snprintf(tmp, sizeof(tmp) - 1, "%d-%d",
				    range_start, range_stop);
			else
				snprintf(tmp, sizeof(tmp) - 1, "%d",
				    range_start);
			strlcat(buf, tmp, buflen);

			range_start = range_stop = devi->pins[i];
			need_coma = 1;
		}
		else
			range_stop++;
	}

	if (need_coma)
		strlcat(buf, ",", buflen);
	memset(tmp, 0, sizeof(tmp));
	if (range_start != range_stop)
		snprintf(tmp, sizeof(tmp) - 1, "%d-%d",
		    range_start, range_stop);
	else
		snprintf(tmp, sizeof(tmp) - 1, "%d",
		    range_start);
	strlcat(buf, tmp, buflen);
}

device_t
gpiobus_attach_bus(device_t dev)
{
	device_t busdev;

	busdev = device_add_child(dev, "gpiobus", -1);
	if (busdev == NULL)
		return (NULL);
	if (device_add_child(dev, "gpioc", -1) == NULL) {
		device_delete_child(dev, busdev);
		return (NULL);
	}
#ifdef FDT
	ofw_gpiobus_register_provider(dev);
#endif
	bus_generic_attach(dev);

	return (busdev);
}

int
gpiobus_detach_bus(device_t dev)
{
	int err;

#ifdef FDT
	ofw_gpiobus_unregister_provider(dev);
#endif
	err = bus_generic_detach(dev);
	if (err != 0)
		return (err);

	return (device_delete_children(dev));
}

int
gpiobus_init_softc(device_t dev)
{
	struct gpiobus_softc *sc;

	sc = GPIOBUS_SOFTC(dev);
	sc->sc_busdev = dev;
	sc->sc_dev = device_get_parent(dev);
	sc->sc_intr_rman.rm_type = RMAN_ARRAY;
	sc->sc_intr_rman.rm_descr = "GPIO Interrupts";
	if (rman_init(&sc->sc_intr_rman) != 0 ||
	    rman_manage_region(&sc->sc_intr_rman, 0, ~0) != 0)
		panic("%s: failed to set up rman.", __func__);

	if (GPIO_PIN_MAX(sc->sc_dev, &sc->sc_npins) != 0)
		return (ENXIO);

	KASSERT(sc->sc_npins >= 0, ("GPIO device with no pins"));

	/* Pins = GPIO_PIN_MAX() + 1 */
	sc->sc_npins++;

	sc->sc_pins = malloc(sizeof(*sc->sc_pins) * sc->sc_npins, M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (sc->sc_pins == NULL)
		return (ENOMEM);

	/* Initialize the bus lock. */
	GPIOBUS_LOCK_INIT(sc);

	return (0);
}

int
gpiobus_alloc_ivars(struct gpiobus_ivar *devi)
{

	/* Allocate pins and flags memory. */
	devi->pins = malloc(sizeof(uint32_t) * devi->npins, M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (devi->pins == NULL)
		return (ENOMEM);
	devi->flags = malloc(sizeof(uint32_t) * devi->npins, M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (devi->flags == NULL) {
		free(devi->pins, M_DEVBUF);
		return (ENOMEM);
	}

	return (0);
}

void
gpiobus_free_ivars(struct gpiobus_ivar *devi)
{

	if (devi->flags) {
		free(devi->flags, M_DEVBUF);
		devi->flags = NULL;
	}
	if (devi->pins) {
		free(devi->pins, M_DEVBUF);
		devi->pins = NULL;
	}
}

int
gpiobus_acquire_pin(device_t bus, uint32_t pin)
{
	struct gpiobus_softc *sc;

	sc = device_get_softc(bus);
	/* Consistency check. */
	if (pin >= sc->sc_npins) {
		device_printf(bus,
		    "invalid pin %d, max: %d\n", pin, sc->sc_npins - 1);
		return (-1);
	}
	/* Mark pin as mapped and give warning if it's already mapped. */
	if (sc->sc_pins[pin].mapped) {
		device_printf(bus, "warning: pin %d is already mapped\n", pin);
		return (-1);
	}
	sc->sc_pins[pin].mapped = 1;

	return (0);
}

/* Release mapped pin */
int
gpiobus_release_pin(device_t bus, uint32_t pin)
{
	struct gpiobus_softc *sc;

	sc = device_get_softc(bus);
	/* Consistency check. */
	if (pin >= sc->sc_npins) {
		device_printf(bus,
		    "gpiobus_acquire_pin: invalid pin %d, max=%d\n",
		    pin, sc->sc_npins - 1);
		return (-1);
	}

	if (!sc->sc_pins[pin].mapped) {
		device_printf(bus, "gpiobus_acquire_pin: pin %d is not mapped\n", pin);
		return (-1);
	}
	sc->sc_pins[pin].mapped = 0;

	return (0);
}

static int
gpiobus_parse_pins(struct gpiobus_softc *sc, device_t child, int mask)
{
	struct gpiobus_ivar *devi = GPIOBUS_IVAR(child);
	int i, npins;

	npins = 0;
	for (i = 0; i < 32; i++) {
		if (mask & (1 << i))
			npins++;
	}
	if (npins == 0) {
		device_printf(child, "empty pin mask\n");
		return (EINVAL);
	}
	devi->npins = npins;
	if (gpiobus_alloc_ivars(devi) != 0) {
		device_printf(child, "cannot allocate device ivars\n");
		return (EINVAL);
	}
	npins = 0;
	for (i = 0; i < 32; i++) {
		if ((mask & (1 << i)) == 0)
			continue;
		/* Reserve the GPIO pin. */
		if (gpiobus_acquire_pin(sc->sc_busdev, i) != 0) {
			gpiobus_free_ivars(devi);
			return (EINVAL);
		}
		devi->pins[npins++] = i;
		/* Use the child name as pin name. */
		GPIOBUS_PIN_SETNAME(sc->sc_busdev, i,
		    device_get_nameunit(child));
	}

	return (0);
}

static int
gpiobus_probe(device_t dev)
{
	device_set_desc(dev, "GPIO bus");

	return (BUS_PROBE_GENERIC);
}

static int
gpiobus_attach(device_t dev)
{
	int err;

	err = gpiobus_init_softc(dev);
	if (err != 0)
		return (err);

	/*
	 * Get parent's pins and mark them as unmapped
	 */
	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);

	return (bus_generic_attach(dev));
}

/*
 * Since this is not a self-enumerating bus, and since we always add
 * children in attach, we have to always delete children here.
 */
static int
gpiobus_detach(device_t dev)
{
	struct gpiobus_softc *sc;
	struct gpiobus_ivar *devi;
	device_t *devlist;
	int i, err, ndevs;

	sc = GPIOBUS_SOFTC(dev);
	KASSERT(mtx_initialized(&sc->sc_mtx),
	    ("gpiobus mutex not initialized"));
	GPIOBUS_LOCK_DESTROY(sc);

	if ((err = bus_generic_detach(dev)) != 0)
		return (err);

	if ((err = device_get_children(dev, &devlist, &ndevs)) != 0)
		return (err);
	for (i = 0; i < ndevs; i++) {
		devi = GPIOBUS_IVAR(devlist[i]);
		gpiobus_free_ivars(devi);
		resource_list_free(&devi->rl);
		free(devi, M_DEVBUF);
		device_delete_child(dev, devlist[i]);
	}
	free(devlist, M_TEMP);
	rman_fini(&sc->sc_intr_rman);
	if (sc->sc_pins) {
		for (i = 0; i < sc->sc_npins; i++) {
			if (sc->sc_pins[i].name != NULL)
				free(sc->sc_pins[i].name, M_DEVBUF);
			sc->sc_pins[i].name = NULL;
		}
		free(sc->sc_pins, M_DEVBUF);
		sc->sc_pins = NULL;
	}

	return (0);
}

static int
gpiobus_suspend(device_t dev)
{

	return (bus_generic_suspend(dev));
}

static int
gpiobus_resume(device_t dev)
{

	return (bus_generic_resume(dev));
}

static void
gpiobus_probe_nomatch(device_t dev, device_t child)
{
	char pins[128];
	struct gpiobus_ivar *devi;

	devi = GPIOBUS_IVAR(child);
	memset(pins, 0, sizeof(pins));
	gpiobus_print_pins(devi, pins, sizeof(pins));
	if (devi->npins > 1)
		device_printf(dev, "<unknown device> at pins %s", pins);
	else
		device_printf(dev, "<unknown device> at pin %s", pins);
	resource_list_print_type(&devi->rl, "irq", SYS_RES_IRQ, "%jd");
	printf("\n");
}

static int
gpiobus_print_child(device_t dev, device_t child)
{
	char pins[128];
	int retval = 0;
	struct gpiobus_ivar *devi;

	devi = GPIOBUS_IVAR(child);
	memset(pins, 0, sizeof(pins));
	retval += bus_print_child_header(dev, child);
	if (devi->npins > 0) {
		if (devi->npins > 1)
			retval += printf(" at pins ");
		else
			retval += printf(" at pin ");
		gpiobus_print_pins(devi, pins, sizeof(pins));
		retval += printf("%s", pins);
	}
	resource_list_print_type(&devi->rl, "irq", SYS_RES_IRQ, "%jd");
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

static int
gpiobus_child_location_str(device_t bus, device_t child, char *buf,
    size_t buflen)
{
	struct gpiobus_ivar *devi;

	devi = GPIOBUS_IVAR(child);
	if (devi->npins > 1)
		strlcpy(buf, "pins=", buflen);
	else
		strlcpy(buf, "pin=", buflen);
	gpiobus_print_pins(devi, buf, buflen);

	return (0);
}

static int
gpiobus_child_pnpinfo_str(device_t bus, device_t child, char *buf,
    size_t buflen)
{

	*buf = '\0';
	return (0);
}

static device_t
gpiobus_add_child(device_t dev, u_int order, const char *name, int unit)
{
	device_t child;
	struct gpiobus_ivar *devi;

	child = device_add_child_ordered(dev, order, name, unit);
	if (child == NULL) 
		return (child);
	devi = malloc(sizeof(struct gpiobus_ivar), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (devi == NULL) {
		device_delete_child(dev, child);
		return (NULL);
	}
	resource_list_init(&devi->rl);
	device_set_ivars(child, devi);

	return (child);
}

static void
gpiobus_hinted_child(device_t bus, const char *dname, int dunit)
{
	struct gpiobus_softc *sc = GPIOBUS_SOFTC(bus);
	struct gpiobus_ivar *devi;
	device_t child;
	int irq, pins;

	child = BUS_ADD_CHILD(bus, 0, dname, dunit);
	devi = GPIOBUS_IVAR(child);
	resource_int_value(dname, dunit, "pins", &pins);
	if (gpiobus_parse_pins(sc, child, pins)) {
		resource_list_free(&devi->rl);
		free(devi, M_DEVBUF);
		device_delete_child(bus, child);
	}
	if (resource_int_value(dname, dunit, "irq", &irq) == 0) {
		if (bus_set_resource(child, SYS_RES_IRQ, 0, irq, 1) != 0)
			device_printf(bus,
			    "warning: bus_set_resource() failed\n");
	}
}

static int
gpiobus_set_resource(device_t dev, device_t child, int type, int rid,
    rman_res_t start, rman_res_t count)
{
	struct gpiobus_ivar *devi;
	struct resource_list_entry *rle;

	dprintf("%s: entry (%p, %p, %d, %d, %p, %ld)\n",
	    __func__, dev, child, type, rid, (void *)(intptr_t)start, count);
	devi = GPIOBUS_IVAR(child);
	rle = resource_list_add(&devi->rl, type, rid, start,
	    start + count - 1, count);
	if (rle == NULL)
		return (ENXIO);

	return (0);
}

static struct resource *
gpiobus_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct gpiobus_softc *sc;
	struct resource *rv;
	struct resource_list *rl;
	struct resource_list_entry *rle;
	int isdefault;

	if (type != SYS_RES_IRQ)
		return (NULL);
	isdefault = (RMAN_IS_DEFAULT_RANGE(start, end) && count == 1);
	rle = NULL;
	if (isdefault) {
		rl = BUS_GET_RESOURCE_LIST(bus, child);
		if (rl == NULL)
			return (NULL);
		rle = resource_list_find(rl, type, *rid);
		if (rle == NULL)
			return (NULL);
		if (rle->res != NULL)
			panic("%s: resource entry is busy", __func__);
		start = rle->start;
		count = rle->count;
		end = rle->end;
	}
	sc = device_get_softc(bus);
	rv = rman_reserve_resource(&sc->sc_intr_rman, start, end, count, flags,
	    child);
	if (rv == NULL)
		return (NULL);
	rman_set_rid(rv, *rid);
	if ((flags & RF_ACTIVE) != 0 &&
	    bus_activate_resource(child, type, *rid, rv) != 0) {
		rman_release_resource(rv);
		return (NULL);
	}

	return (rv);
}

static int
gpiobus_release_resource(device_t bus __unused, device_t child, int type,
    int rid, struct resource *r)
{
	int error;

	if (rman_get_flags(r) & RF_ACTIVE) {
		error = bus_deactivate_resource(child, type, rid, r);
		if (error)
			return (error);
	}

	return (rman_release_resource(r));
}

static struct resource_list *
gpiobus_get_resource_list(device_t bus __unused, device_t child)
{
	struct gpiobus_ivar *ivar;

	ivar = GPIOBUS_IVAR(child);

	return (&ivar->rl);
}

static int
gpiobus_acquire_bus(device_t busdev, device_t child, int how)
{
	struct gpiobus_softc *sc;

	sc = device_get_softc(busdev);
	GPIOBUS_ASSERT_UNLOCKED(sc);
	GPIOBUS_LOCK(sc);
	if (sc->sc_owner != NULL) {
		if (sc->sc_owner == child)
			panic("%s: %s still owns the bus.",
			    device_get_nameunit(busdev),
			    device_get_nameunit(child));
		if (how == GPIOBUS_DONTWAIT) {
			GPIOBUS_UNLOCK(sc);
			return (EWOULDBLOCK);
		}
		while (sc->sc_owner != NULL)
			mtx_sleep(sc, &sc->sc_mtx, 0, "gpiobuswait", 0);
	}
	sc->sc_owner = child;
	GPIOBUS_UNLOCK(sc);

	return (0);
}

static void
gpiobus_release_bus(device_t busdev, device_t child)
{
	struct gpiobus_softc *sc;

	sc = device_get_softc(busdev);
	GPIOBUS_ASSERT_UNLOCKED(sc);
	GPIOBUS_LOCK(sc);
	if (sc->sc_owner == NULL)
		panic("%s: %s releasing unowned bus.",
		    device_get_nameunit(busdev),
		    device_get_nameunit(child));
	if (sc->sc_owner != child)
		panic("%s: %s trying to release bus owned by %s",
		    device_get_nameunit(busdev),
		    device_get_nameunit(child),
		    device_get_nameunit(sc->sc_owner));
	sc->sc_owner = NULL;
	wakeup(sc);
	GPIOBUS_UNLOCK(sc);
}

static int
gpiobus_pin_setflags(device_t dev, device_t child, uint32_t pin, 
    uint32_t flags)
{
	struct gpiobus_softc *sc = GPIOBUS_SOFTC(dev);
	struct gpiobus_ivar *devi = GPIOBUS_IVAR(child);
	uint32_t caps;

	if (pin >= devi->npins)
		return (EINVAL);
	if (GPIO_PIN_GETCAPS(sc->sc_dev, devi->pins[pin], &caps) != 0)
		return (EINVAL);
	if (gpio_check_flags(caps, flags) != 0)
		return (EINVAL);

	return (GPIO_PIN_SETFLAGS(sc->sc_dev, devi->pins[pin], flags));
}

static int
gpiobus_pin_getflags(device_t dev, device_t child, uint32_t pin, 
    uint32_t *flags)
{
	struct gpiobus_softc *sc = GPIOBUS_SOFTC(dev);
	struct gpiobus_ivar *devi = GPIOBUS_IVAR(child);

	if (pin >= devi->npins)
		return (EINVAL);

	return GPIO_PIN_GETFLAGS(sc->sc_dev, devi->pins[pin], flags);
}

static int
gpiobus_pin_getcaps(device_t dev, device_t child, uint32_t pin, 
    uint32_t *caps)
{
	struct gpiobus_softc *sc = GPIOBUS_SOFTC(dev);
	struct gpiobus_ivar *devi = GPIOBUS_IVAR(child);

	if (pin >= devi->npins)
		return (EINVAL);

	return GPIO_PIN_GETCAPS(sc->sc_dev, devi->pins[pin], caps);
}

static int
gpiobus_pin_set(device_t dev, device_t child, uint32_t pin, 
    unsigned int value)
{
	struct gpiobus_softc *sc = GPIOBUS_SOFTC(dev);
	struct gpiobus_ivar *devi = GPIOBUS_IVAR(child);

	if (pin >= devi->npins)
		return (EINVAL);

	return GPIO_PIN_SET(sc->sc_dev, devi->pins[pin], value);
}

static int
gpiobus_pin_get(device_t dev, device_t child, uint32_t pin, 
    unsigned int *value)
{
	struct gpiobus_softc *sc = GPIOBUS_SOFTC(dev);
	struct gpiobus_ivar *devi = GPIOBUS_IVAR(child);

	if (pin >= devi->npins)
		return (EINVAL);

	return GPIO_PIN_GET(sc->sc_dev, devi->pins[pin], value);
}

static int
gpiobus_pin_toggle(device_t dev, device_t child, uint32_t pin)
{
	struct gpiobus_softc *sc = GPIOBUS_SOFTC(dev);
	struct gpiobus_ivar *devi = GPIOBUS_IVAR(child);

	if (pin >= devi->npins)
		return (EINVAL);

	return GPIO_PIN_TOGGLE(sc->sc_dev, devi->pins[pin]);
}

static int
gpiobus_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct gpiobus_softc *sc;

	sc = GPIOBUS_SOFTC(dev);
	if (pin > sc->sc_npins)
		return (EINVAL);
	/* Did we have a name for this pin ? */
	if (sc->sc_pins[pin].name != NULL) {
		memcpy(name, sc->sc_pins[pin].name, GPIOMAXNAME);
		return (0);
	}

	/* Return the default pin name. */
	return (GPIO_PIN_GETNAME(device_get_parent(dev), pin, name));
}

static int
gpiobus_pin_setname(device_t dev, uint32_t pin, const char *name)
{
	struct gpiobus_softc *sc;

	sc = GPIOBUS_SOFTC(dev);
	if (pin > sc->sc_npins)
		return (EINVAL);
	if (name == NULL)
		return (EINVAL);
	/* Save the pin name. */
	if (sc->sc_pins[pin].name == NULL)
		sc->sc_pins[pin].name = malloc(GPIOMAXNAME, M_DEVBUF,
		    M_WAITOK | M_ZERO);
	strlcpy(sc->sc_pins[pin].name, name, GPIOMAXNAME);

	return (0);
}

static device_method_t gpiobus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		gpiobus_probe),
	DEVMETHOD(device_attach,	gpiobus_attach),
	DEVMETHOD(device_detach,	gpiobus_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	gpiobus_suspend),
	DEVMETHOD(device_resume,	gpiobus_resume),

	/* Bus interface */
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_config_intr,	bus_generic_config_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_set_resource,	gpiobus_set_resource),
	DEVMETHOD(bus_alloc_resource,	gpiobus_alloc_resource),
	DEVMETHOD(bus_release_resource,	gpiobus_release_resource),
	DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),
	DEVMETHOD(bus_get_resource_list,	gpiobus_get_resource_list),
	DEVMETHOD(bus_add_child,	gpiobus_add_child),
	DEVMETHOD(bus_probe_nomatch,	gpiobus_probe_nomatch),
	DEVMETHOD(bus_print_child,	gpiobus_print_child),
	DEVMETHOD(bus_child_pnpinfo_str, gpiobus_child_pnpinfo_str),
	DEVMETHOD(bus_child_location_str, gpiobus_child_location_str),
	DEVMETHOD(bus_hinted_child,	gpiobus_hinted_child),

	/* GPIO protocol */
	DEVMETHOD(gpiobus_acquire_bus,	gpiobus_acquire_bus),
	DEVMETHOD(gpiobus_release_bus,	gpiobus_release_bus),
	DEVMETHOD(gpiobus_pin_getflags,	gpiobus_pin_getflags),
	DEVMETHOD(gpiobus_pin_getcaps,	gpiobus_pin_getcaps),
	DEVMETHOD(gpiobus_pin_setflags,	gpiobus_pin_setflags),
	DEVMETHOD(gpiobus_pin_get,	gpiobus_pin_get),
	DEVMETHOD(gpiobus_pin_set,	gpiobus_pin_set),
	DEVMETHOD(gpiobus_pin_toggle,	gpiobus_pin_toggle),
	DEVMETHOD(gpiobus_pin_getname,	gpiobus_pin_getname),
	DEVMETHOD(gpiobus_pin_setname,	gpiobus_pin_setname),

	DEVMETHOD_END
};

driver_t gpiobus_driver = {
	"gpiobus",
	gpiobus_methods,
	sizeof(struct gpiobus_softc)
};

devclass_t	gpiobus_devclass;

EARLY_DRIVER_MODULE(gpiobus, gpio, gpiobus_driver, gpiobus_devclass, 0, 0,
    BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
MODULE_VERSION(gpiobus, 1);
