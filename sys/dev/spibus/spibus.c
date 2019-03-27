/*-
 * Copyright (c) 2006 M. Warner Losh
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
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/spibus/spibusvar.h>
#include <dev/spibus/spi.h>
#include "spibus_if.h"

static int
spibus_probe(device_t dev)
{

	device_set_desc(dev, "SPI bus");
	return (BUS_PROBE_DEFAULT);
}

static int
spibus_attach(device_t dev)
{
	struct spibus_softc *sc = SPIBUS_SOFTC(dev);

	sc->dev = dev;
	bus_enumerate_hinted_children(dev);
	return (bus_generic_attach(dev));
}

/*
 * Since this is not a self-enumerating bus, and since we always add
 * children in attach, we have to always delete children here.
 */
static int
spibus_detach(device_t dev)
{
	int err;

	if ((err = bus_generic_detach(dev)) != 0)
		return (err);
	device_delete_children(dev);

	return (0);
}

static int
spibus_suspend(device_t dev)
{
	return (bus_generic_suspend(dev));
}

static
int
spibus_resume(device_t dev)
{
	return (bus_generic_resume(dev));
}

static int
spibus_print_child(device_t dev, device_t child)
{
	struct spibus_ivar *devi = SPIBUS_IVAR(child);
	int retval = 0;

	retval += bus_print_child_header(dev, child);
	retval += printf(" at cs %d", devi->cs);
	retval += printf(" mode %d", devi->mode);
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

static void
spibus_probe_nomatch(device_t bus, device_t child)
{
	struct spibus_ivar *devi = SPIBUS_IVAR(child);

	device_printf(bus, "<unknown card> at cs %d mode %d\n", devi->cs,
	    devi->mode);
	return;
}

static int
spibus_child_location_str(device_t bus, device_t child, char *buf,
    size_t buflen)
{
	struct spibus_ivar *devi = SPIBUS_IVAR(child);
	int cs;

	cs = devi->cs & ~SPIBUS_CS_HIGH; /* trim 'cs high' bit */
	snprintf(buf, buflen, "bus=%d cs=%d", device_get_unit(bus), cs);
	return (0);
}

static int
spibus_child_pnpinfo_str(device_t bus, device_t child, char *buf,
    size_t buflen)
{
	*buf = '\0';
	return (0);
}

static int
spibus_read_ivar(device_t bus, device_t child, int which, uintptr_t *result)
{
	struct spibus_ivar *devi = SPIBUS_IVAR(child);

	switch (which) {
	default:
		return (EINVAL);
	case SPIBUS_IVAR_CS:
		*(uint32_t *)result = devi->cs;
		break;
	case SPIBUS_IVAR_MODE:
		*(uint32_t *)result = devi->mode;
		break;
	case SPIBUS_IVAR_CLOCK:
		*(uint32_t *)result = devi->clock;
		break;
	}
	return (0);
}

static int
spibus_write_ivar(device_t bus, device_t child, int which, uintptr_t value)
{
	struct spibus_ivar *devi = SPIBUS_IVAR(child);

	if (devi == NULL || device_get_parent(child) != bus)
		return (EDOOFUS);

	switch (which) {
	case SPIBUS_IVAR_CLOCK:
		/* Any non-zero value is allowed for max clock frequency. */
		if (value == 0)
			return (EINVAL);
		devi->clock = (uint32_t)value;
		break;
	case SPIBUS_IVAR_CS:
		 /* Chip select cannot be changed. */
		return (EINVAL);
	case SPIBUS_IVAR_MODE:
		/* Valid SPI modes are 0-3. */
		if (value > 3)
			return (EINVAL);
		devi->mode = (uint32_t)value;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

static device_t
spibus_add_child(device_t dev, u_int order, const char *name, int unit)
{
	device_t child;
	struct spibus_ivar *devi;

	child = device_add_child_ordered(dev, order, name, unit);
	if (child == NULL) 
		return (child);
	devi = malloc(sizeof(struct spibus_ivar), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (devi == NULL) {
		device_delete_child(dev, child);
		return (0);
	}
	device_set_ivars(child, devi);
	return (child);
}

static void
spibus_hinted_child(device_t bus, const char *dname, int dunit)
{
	device_t child;
	struct spibus_ivar *devi;

	child = BUS_ADD_CHILD(bus, 0, dname, dunit);
	devi = SPIBUS_IVAR(child);
	devi->mode = SPIBUS_MODE_NONE;
	resource_int_value(dname, dunit, "clock", &devi->clock);
	resource_int_value(dname, dunit, "cs", &devi->cs);
	resource_int_value(dname, dunit, "mode", &devi->mode);
}

static int
spibus_transfer_impl(device_t dev, device_t child, struct spi_command *cmd)
{
	return (SPIBUS_TRANSFER(device_get_parent(dev), child, cmd));
}

static device_method_t spibus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		spibus_probe),
	DEVMETHOD(device_attach,	spibus_attach),
	DEVMETHOD(device_detach,	spibus_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	spibus_suspend),
	DEVMETHOD(device_resume,	spibus_resume),

	/* Bus interface */
	DEVMETHOD(bus_add_child,	spibus_add_child),
	DEVMETHOD(bus_print_child,	spibus_print_child),
	DEVMETHOD(bus_probe_nomatch,	spibus_probe_nomatch),
	DEVMETHOD(bus_read_ivar,	spibus_read_ivar),
	DEVMETHOD(bus_write_ivar,	spibus_write_ivar),
	DEVMETHOD(bus_child_pnpinfo_str, spibus_child_pnpinfo_str),
	DEVMETHOD(bus_child_location_str, spibus_child_location_str),
	DEVMETHOD(bus_hinted_child,	spibus_hinted_child),

	/* spibus interface */
	DEVMETHOD(spibus_transfer,	spibus_transfer_impl),

	DEVMETHOD_END
};

driver_t spibus_driver = {
	"spibus",
	spibus_methods,
	sizeof(struct spibus_softc)
};

devclass_t	spibus_devclass;

DRIVER_MODULE(spibus, spi, spibus_driver, spibus_devclass, 0, 0);
MODULE_VERSION(spibus, 1);
