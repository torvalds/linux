/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 2001 Nicolas Souchu
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/bus.h>

#include <dev/smbus/smbconf.h>
#include <dev/smbus/smbus.h>

#include "smbus_if.h"
#include "bus_if.h"

struct smbus_ivar
{
	uint8_t	addr;
};

/*
 * Autoconfiguration and support routines for System Management bus
 */

static int
smbus_probe(device_t dev)
{

	device_set_desc(dev, "System Management Bus");

	return (0);
}

static int
smbus_attach(device_t dev)
{
	struct smbus_softc *sc = device_get_softc(dev);

	mtx_init(&sc->lock, device_get_nameunit(dev), "smbus", MTX_DEF);
	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	bus_generic_attach(dev);

	return (0);
}

static int
smbus_detach(device_t dev)
{
	struct smbus_softc *sc = device_get_softc(dev);
	int error;

	error = bus_generic_detach(dev);
	if (error)
		return (error);
	device_delete_children(dev);
	mtx_destroy(&sc->lock);

	return (0);
}

void
smbus_generic_intr(device_t dev, u_char devaddr, char low, char high, int err)
{
}

static device_t
smbus_add_child(device_t dev, u_int order, const char *name, int unit)
{
	struct smbus_ivar *devi;
	device_t child;

	child = device_add_child_ordered(dev, order, name, unit);
	if (child == NULL)
		return (child);
	devi = malloc(sizeof(struct smbus_ivar), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (devi == NULL) {
		device_delete_child(dev, child);
		return (NULL);
	}
	device_set_ivars(child, devi);
	return (child);
}

static void
smbus_hinted_child(device_t bus, const char *dname, int dunit)
{
	struct smbus_ivar *devi;
	device_t child;
	int addr;

	addr = 0;
	resource_int_value(dname, dunit, "addr", &addr);
	if (addr > UINT8_MAX) {
		device_printf(bus, "ignored incorrect slave address hint 0x%x"
		    " for %s%d\n", addr, dname, dunit);
		return;
	}
	child = BUS_ADD_CHILD(bus, SMBUS_ORDER_HINTED, dname, dunit);
	if (child == NULL)
		return;
	devi = device_get_ivars(child);
	devi->addr = addr;
}


static int
smbus_child_location_str(device_t parent, device_t child, char *buf,
    size_t buflen)
{
	struct smbus_ivar *devi;

	devi = device_get_ivars(child);
	if (devi->addr != 0)
		snprintf(buf, buflen, "addr=0x%x", devi->addr);
	else if (buflen)
		buf[0] = 0;
	return (0);
}

static int
smbus_print_child(device_t parent, device_t child)
{
	struct smbus_ivar *devi;
	int retval;

	devi = device_get_ivars(child);
	retval = bus_print_child_header(parent, child);
	if (devi->addr != 0)
		retval += printf(" at addr 0x%x", devi->addr);
	retval += bus_print_child_footer(parent, child);

	return (retval);
}

static int
smbus_read_ivar(device_t parent, device_t child, int which, uintptr_t *result)
{
	struct smbus_ivar *devi;

	devi = device_get_ivars(child);
	switch (which) {
	case SMBUS_IVAR_ADDR:
		if (devi->addr != 0)
			*result = devi->addr;
		else
			*result = -1;
		break;
	default:
		return (ENOENT);
	}
	return (0);
}

static int
smbus_write_ivar(device_t parent, device_t child, int which, uintptr_t value)
{
	struct smbus_ivar *devi;

	devi = device_get_ivars(child);
	switch (which) {
	case SMBUS_IVAR_ADDR:
		/* Allow to set but no change the slave address. */
		if (devi->addr != 0)
			return (EINVAL);
		devi->addr = value;
		break;
	default:
		return (ENOENT);
	}
	return (0);
}

static void
smbus_probe_nomatch(device_t bus, device_t child)
{
	struct smbus_ivar *devi = device_get_ivars(child);

	/*
	 * Ignore (self-identified) devices without a slave address set.
	 * For example, smb(4).
	 */
	if (devi->addr != 0)
		device_printf(bus, "<unknown device> at addr %#x\n",
		    devi->addr);
}

/*
 * Device methods
 */
static device_method_t smbus_methods[] = {
        /* device interface */
        DEVMETHOD(device_probe,         smbus_probe),
        DEVMETHOD(device_attach,        smbus_attach),
        DEVMETHOD(device_detach,        smbus_detach),

	/* bus interface */
	DEVMETHOD(bus_add_child,	smbus_add_child),
	DEVMETHOD(bus_hinted_child,	smbus_hinted_child),
	DEVMETHOD(bus_probe_nomatch,	smbus_probe_nomatch),
	DEVMETHOD(bus_child_location_str, smbus_child_location_str),
	DEVMETHOD(bus_print_child,	smbus_print_child),
	DEVMETHOD(bus_read_ivar,	smbus_read_ivar),
	DEVMETHOD(bus_write_ivar,	smbus_write_ivar),

	DEVMETHOD_END
};

driver_t smbus_driver = {
        "smbus",
        smbus_methods,
        sizeof(struct smbus_softc),
};

devclass_t smbus_devclass;

MODULE_VERSION(smbus, SMBUS_MODVER);
