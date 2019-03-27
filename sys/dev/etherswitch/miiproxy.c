/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2012 Stefan Bethke.
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/etherswitch/miiproxy.h>
#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include "mdio_if.h"
#include "miibus_if.h"


MALLOC_DECLARE(M_MIIPROXY);
MALLOC_DEFINE(M_MIIPROXY, "miiproxy", "miiproxy data structures");

driver_t miiproxy_driver;
driver_t mdioproxy_driver;

struct miiproxy_softc {
	device_t	parent;
	device_t	proxy;
	device_t	mdio;
};

struct mdioproxy_softc {
};

/*
 * The rendezvous data structures and functions allow two device endpoints to
 * match up, so that the proxy endpoint can be associated with a target
 * endpoint.  The proxy has to know the device name of the target that it
 * wants to associate with, for example through a hint.  The rendezvous code
 * makes no assumptions about the devices that want to meet.
 */
struct rendezvous_entry;

enum rendezvous_op {
	RENDEZVOUS_ATTACH,
	RENDEZVOUS_DETACH
};

typedef int (*rendezvous_callback_t)(enum rendezvous_op,
    struct rendezvous_entry *);

static SLIST_HEAD(rendezvoushead, rendezvous_entry) rendezvoushead =
    SLIST_HEAD_INITIALIZER(rendezvoushead);

struct rendezvous_endpoint {
	device_t		device;
	const char		*name;
	rendezvous_callback_t	callback;
};

struct rendezvous_entry {
	SLIST_ENTRY(rendezvous_entry)	entries;
	struct rendezvous_endpoint	proxy;
	struct rendezvous_endpoint	target;
};

/*
 * Call the callback routines for both the proxy and the target.  If either
 * returns an error, undo the attachment.
 */
static int
rendezvous_attach(struct rendezvous_entry *e, struct rendezvous_endpoint *ep)
{
	int error;

	error = e->proxy.callback(RENDEZVOUS_ATTACH, e);
	if (error == 0) {
		error = e->target.callback(RENDEZVOUS_ATTACH, e);
		if (error != 0) {
			e->proxy.callback(RENDEZVOUS_DETACH, e);
			ep->device = NULL;
			ep->callback = NULL;
		}
	}
	return (error);
}

/*
 * Create an entry for the proxy in the rendezvous list.  The name parameter
 * indicates the name of the device that is the target endpoint for this
 * rendezvous.  The callback will be invoked as soon as the target is
 * registered: either immediately if the target registered itself earlier,
 * or once the target registers.  Returns ENXIO if the target has not yet
 * registered.
 */
static int
rendezvous_register_proxy(device_t dev, const char *name,
    rendezvous_callback_t callback)
{
	struct rendezvous_entry *e;

	KASSERT(callback != NULL, ("callback must be set"));
	SLIST_FOREACH(e, &rendezvoushead, entries) {
		if (strcmp(name, e->target.name) == 0) {
			/* the target is already attached */
			e->proxy.name = device_get_nameunit(dev);
		    	e->proxy.device = dev;
		    	e->proxy.callback = callback;
			return (rendezvous_attach(e, &e->proxy));
		}
	}
	e = malloc(sizeof(*e), M_MIIPROXY, M_WAITOK | M_ZERO);
	e->proxy.name = device_get_nameunit(dev);
    	e->proxy.device = dev;
    	e->proxy.callback = callback;
	e->target.name = name;
	SLIST_INSERT_HEAD(&rendezvoushead, e, entries);
	return (ENXIO);
}

/*
 * Create an entry in the rendezvous list for the target.
 * Returns ENXIO if the proxy has not yet registered.
 */
static int
rendezvous_register_target(device_t dev, rendezvous_callback_t callback)
{
	struct rendezvous_entry *e;
	const char *name;

	KASSERT(callback != NULL, ("callback must be set"));
	name = device_get_nameunit(dev);
	SLIST_FOREACH(e, &rendezvoushead, entries) {
		if (strcmp(name, e->target.name) == 0) {
			e->target.device = dev;
			e->target.callback = callback;
			return (rendezvous_attach(e, &e->target));
		}
	}
	e = malloc(sizeof(*e), M_MIIPROXY, M_WAITOK | M_ZERO);
	e->target.name = name;
    	e->target.device = dev;
	e->target.callback = callback;
	SLIST_INSERT_HEAD(&rendezvoushead, e, entries);
	return (ENXIO);
}

/*
 * Remove the registration for the proxy.
 */
static int
rendezvous_unregister_proxy(device_t dev)
{
	struct rendezvous_entry *e;
	int error = 0;

	SLIST_FOREACH(e, &rendezvoushead, entries) {
		if (e->proxy.device == dev) {
			if (e->target.device == NULL) {
				SLIST_REMOVE(&rendezvoushead, e, rendezvous_entry, entries);
				free(e, M_MIIPROXY);
				return (0);
			} else {
				e->proxy.callback(RENDEZVOUS_DETACH, e);
				e->target.callback(RENDEZVOUS_DETACH, e);
			}
			e->proxy.device = NULL;
			e->proxy.callback = NULL;
			return (error);
		}
	}
	return (ENOENT);
}

/*
 * Remove the registration for the target.
 */
static int
rendezvous_unregister_target(device_t dev)
{
	struct rendezvous_entry *e;
	int error = 0;

	SLIST_FOREACH(e, &rendezvoushead, entries) {
		if (e->target.device == dev) {
			if (e->proxy.device == NULL) {
				SLIST_REMOVE(&rendezvoushead, e, rendezvous_entry, entries);
				free(e, M_MIIPROXY);
				return (0);
			} else {
				e->proxy.callback(RENDEZVOUS_DETACH, e);
				e->target.callback(RENDEZVOUS_DETACH, e);
			}
			e->target.device = NULL;
			e->target.callback = NULL;
			return (error);
		}
	}
	return (ENOENT);
}

/*
 * Functions of the proxy that is interposed between the ethernet interface
 * driver and the miibus device.
 */

static int
miiproxy_rendezvous_callback(enum rendezvous_op op, struct rendezvous_entry *rendezvous)
{
	struct miiproxy_softc *sc = device_get_softc(rendezvous->proxy.device);

	switch (op) {
	case RENDEZVOUS_ATTACH:
		sc->mdio = device_get_parent(rendezvous->target.device);
		break;
	case RENDEZVOUS_DETACH:
		sc->mdio = NULL;
		break;
	}
	return (0);
}

static int
miiproxy_probe(device_t dev)
{
	device_set_desc(dev, "MII/MDIO proxy, MII side");

	return (BUS_PROBE_SPECIFIC);
}

static int
miiproxy_attach(device_t dev)
{

	/*
	 * The ethernet interface needs to call mii_attach_proxy() to pass
	 * the relevant parameters for rendezvous with the MDIO target.
	 */
	return (bus_generic_attach(dev));
}

static int
miiproxy_detach(device_t dev)
{

	rendezvous_unregister_proxy(dev);
	bus_generic_detach(dev);
	return (0);
}

static int
miiproxy_readreg(device_t dev, int phy, int reg)
{
	struct miiproxy_softc *sc = device_get_softc(dev);

	if (sc->mdio != NULL)
		return (MDIO_READREG(sc->mdio, phy, reg));
	return (-1);
}

static int
miiproxy_writereg(device_t dev, int phy, int reg, int val)
{
	struct miiproxy_softc *sc = device_get_softc(dev);

	if (sc->mdio != NULL)
		return (MDIO_WRITEREG(sc->mdio, phy, reg, val));
	return (-1);
}

static void
miiproxy_statchg(device_t dev)
{

	MIIBUS_STATCHG(device_get_parent(dev));
}

static void
miiproxy_linkchg(device_t dev)
{

	MIIBUS_LINKCHG(device_get_parent(dev));
}

static void
miiproxy_mediainit(device_t dev)
{

	MIIBUS_MEDIAINIT(device_get_parent(dev));
}

/*
 * Functions for the MDIO target device driver.
 */
static int
mdioproxy_rendezvous_callback(enum rendezvous_op op, struct rendezvous_entry *rendezvous)
{
	return (0);
}

static void
mdioproxy_identify(driver_t *driver, device_t parent)
{
	device_t child;

	if (device_find_child(parent, driver->name, -1) == NULL) {
		child = BUS_ADD_CHILD(parent, 0, driver->name, -1);
	}
}

static int
mdioproxy_probe(device_t dev)
{
	device_set_desc(dev, "MII/MDIO proxy, MDIO side");

	return (BUS_PROBE_SPECIFIC);
}

static int
mdioproxy_attach(device_t dev)
{

	rendezvous_register_target(dev, mdioproxy_rendezvous_callback);
	return (bus_generic_attach(dev));
}

static int
mdioproxy_detach(device_t dev)
{

	rendezvous_unregister_target(dev);
	bus_generic_detach(dev);
	return (0);
}

/*
 * Attach this proxy in place of miibus.  The target MDIO must be attached
 * already.  Returns NULL on error.
 */
device_t
mii_attach_proxy(device_t dev)
{
	struct miiproxy_softc *sc;
	int		error;
	const char	*name;
	device_t	miiproxy;

	if (resource_string_value(device_get_name(dev),
	    device_get_unit(dev), "mdio", &name) != 0) {
	    	if (bootverbose)
			printf("mii_attach_proxy: not attaching, no mdio"
			    " device hint for %s\n", device_get_nameunit(dev));
		return (NULL);
	}

	miiproxy = device_add_child(dev, miiproxy_driver.name, -1);
	error = bus_generic_attach(dev);
	if (error != 0) {
		device_printf(dev, "can't attach miiproxy\n");
		return (NULL);
	}
	sc = device_get_softc(miiproxy);
	sc->parent = dev;
	sc->proxy = miiproxy;
	if (rendezvous_register_proxy(miiproxy, name, miiproxy_rendezvous_callback) != 0) {
		device_printf(dev, "can't attach proxy\n");
		return (NULL);
	}
	device_printf(miiproxy, "attached to target %s\n", device_get_nameunit(sc->mdio));
	return (miiproxy);
}

static device_method_t miiproxy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		miiproxy_probe),
	DEVMETHOD(device_attach,	miiproxy_attach),
	DEVMETHOD(device_detach,	miiproxy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	miiproxy_readreg),
	DEVMETHOD(miibus_writereg,	miiproxy_writereg),
	DEVMETHOD(miibus_statchg,	miiproxy_statchg),
	DEVMETHOD(miibus_linkchg,	miiproxy_linkchg),
	DEVMETHOD(miibus_mediainit,	miiproxy_mediainit),

	DEVMETHOD_END
};

static device_method_t mdioproxy_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	mdioproxy_identify),
	DEVMETHOD(device_probe,		mdioproxy_probe),
	DEVMETHOD(device_attach,	mdioproxy_attach),
	DEVMETHOD(device_detach,	mdioproxy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	DEVMETHOD_END
};

DEFINE_CLASS_0(miiproxy, miiproxy_driver, miiproxy_methods,
    sizeof(struct miiproxy_softc));
DEFINE_CLASS_0(mdioproxy, mdioproxy_driver, mdioproxy_methods,
    sizeof(struct mdioproxy_softc));

devclass_t miiproxy_devclass;
static devclass_t mdioproxy_devclass;

DRIVER_MODULE(mdioproxy, mdio, mdioproxy_driver, mdioproxy_devclass, 0, 0);
DRIVER_MODULE(miibus, miiproxy, miibus_driver, miibus_devclass, 0, 0);
MODULE_VERSION(miiproxy, 1);
MODULE_DEPEND(miiproxy, miibus, 1, 1, 1);
