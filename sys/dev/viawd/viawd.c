/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Fabien Thomas <fabient@FreeBSD.org>
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
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>
#include <sys/watchdog.h>

#include <isa/isavar.h>
#include <dev/pci/pcivar.h>

#include "viawd.h"

#define	viawd_read_4(sc, off)	bus_read_4((sc)->wd_res, (off))
#define	viawd_write_4(sc, off, val)	\
	bus_write_4((sc)->wd_res, (off), (val))

static struct viawd_device viawd_devices[] = {
	{ DEVICEID_VT8251, "VIA VT8251 watchdog timer" },
	{ DEVICEID_CX700,  "VIA CX700 watchdog timer" },
	{ DEVICEID_VX800,  "VIA VX800 watchdog timer" },
	{ DEVICEID_VX855,  "VIA VX855 watchdog timer" },
	{ DEVICEID_VX900,  "VIA VX900 watchdog timer" },
	{ 0, NULL },
};

static devclass_t viawd_devclass;

static void
viawd_tmr_state(struct viawd_softc *sc, int enable)
{
	uint32_t reg;

	reg = viawd_read_4(sc, VIAWD_MEM_CTRL);
	if (enable)
		reg |= VIAWD_MEM_CTRL_TRIGGER | VIAWD_MEM_CTRL_ENABLE;
	else
		reg &= ~VIAWD_MEM_CTRL_ENABLE;
	viawd_write_4(sc, VIAWD_MEM_CTRL, reg);
}

static void
viawd_tmr_set(struct viawd_softc *sc, unsigned int timeout)
{

	/* Keep value in range. */
	if (timeout < VIAWD_MEM_COUNT_MIN)
		timeout = VIAWD_MEM_COUNT_MIN;
	else if (timeout > VIAWD_MEM_COUNT_MAX)
		timeout = VIAWD_MEM_COUNT_MAX;

	viawd_write_4(sc, VIAWD_MEM_COUNT, timeout);
	sc->timeout = timeout;
}

/*
 * Watchdog event handler - called by the framework to enable or disable
 * the watchdog or change the initial timeout value.
 */
static void
viawd_event(void *arg, unsigned int cmd, int *error)
{
	struct viawd_softc *sc = arg;
	unsigned int timeout;

	/* Convert from power-of-two-ns to second. */
	cmd &= WD_INTERVAL;
	timeout = ((uint64_t)1 << cmd) / 1000000000;
	if (cmd) {
		if (timeout != sc->timeout)
			viawd_tmr_set(sc, timeout);
		viawd_tmr_state(sc, 1);
		*error = 0;
	} else
		viawd_tmr_state(sc, 0);
}

/* Look for a supported VIA south bridge. */
static struct viawd_device *
viawd_find(device_t dev)
{
	struct viawd_device *id;

	if (pci_get_vendor(dev) != VENDORID_VIA)
		return (NULL);
	for (id = viawd_devices; id->desc != NULL; id++)
		if (pci_get_device(dev) == id->device)
			return (id);
	return (NULL);
}

static void
viawd_identify(driver_t *driver, device_t parent)
{

	if (viawd_find(parent) == NULL)
		return;

	if (device_find_child(parent, driver->name, -1) == NULL)
		BUS_ADD_CHILD(parent, 0, driver->name, 0);
}

static int
viawd_probe(device_t dev)
{
	struct viawd_device *id;

	id = viawd_find(device_get_parent(dev));
	KASSERT(id != NULL, ("parent should be a valid VIA SB"));
	device_set_desc(dev, id->desc);
	return (BUS_PROBE_GENERIC);
}

static int
viawd_attach(device_t dev)
{
	device_t sb_dev;
	struct viawd_softc *sc;
	uint32_t pmbase, reg;

	sc = device_get_softc(dev);
	sc->dev = dev;

	sb_dev = device_get_parent(dev);
	if (sb_dev == NULL) {
		device_printf(dev, "Can not find watchdog device.\n");
		goto fail;
	}
	sc->sb_dev = sb_dev;

	/* Get watchdog memory base. */
	pmbase = pci_read_config(sb_dev, VIAWD_CONFIG_BASE, 4);
	if (pmbase == 0) {
		device_printf(dev,
		    "Watchdog disabled in BIOS or hardware\n");
		goto fail;
	}

	/* Allocate I/O register space. */
	sc->wd_rid = VIAWD_CONFIG_BASE;
	sc->wd_res = bus_alloc_resource_any(sb_dev, SYS_RES_MEMORY, &sc->wd_rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->wd_res == NULL) {
		device_printf(dev, "Unable to map watchdog memory\n");
		goto fail;
	}
	if (rman_get_size(sc->wd_res) < VIAWD_MEM_LEN) {
		device_printf(dev, "Bad size for watchdog memory: %#x\n",
		    (unsigned)rman_get_size(sc->wd_res));
		goto fail;
	}

	/* Check if watchdog fired last boot. */
	reg = viawd_read_4(sc, VIAWD_MEM_CTRL);
	if (reg & VIAWD_MEM_CTRL_FIRED) {
		device_printf(dev,
		    "ERROR: watchdog rebooted the system\n");
		/* Reset bit state. */
		viawd_write_4(sc, VIAWD_MEM_CTRL, reg);
	}

	/* Register the watchdog event handler. */
	sc->ev_tag = EVENTHANDLER_REGISTER(watchdog_list, viawd_event, sc, 0);

	return (0);
fail:
	if (sc->wd_res != NULL)
		bus_release_resource(sb_dev, SYS_RES_MEMORY,
		    sc->wd_rid, sc->wd_res);
	return (ENXIO);
}

static int
viawd_detach(device_t dev)
{
	struct viawd_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	/* Deregister event handler. */
	if (sc->ev_tag != NULL)
		EVENTHANDLER_DEREGISTER(watchdog_list, sc->ev_tag);
	sc->ev_tag = NULL;

	/*
	 * Do not stop the watchdog on shutdown if active but bump the
	 * timer to avoid spurious reset.
	 */
	reg = viawd_read_4(sc, VIAWD_MEM_CTRL);
	if (reg & VIAWD_MEM_CTRL_ENABLE) {
		viawd_tmr_set(sc, VIAWD_TIMEOUT_SHUTDOWN);
		viawd_tmr_state(sc, 1);
		device_printf(dev,
		    "Keeping watchog alive during shutdown for %d seconds\n",
		    VIAWD_TIMEOUT_SHUTDOWN);
	}

	if (sc->wd_res != NULL)
		bus_release_resource(sc->sb_dev, SYS_RES_MEMORY,
		    sc->wd_rid, sc->wd_res);

	return (0);
}

static device_method_t viawd_methods[] = {
	DEVMETHOD(device_identify, viawd_identify),
	DEVMETHOD(device_probe,	viawd_probe),
	DEVMETHOD(device_attach, viawd_attach),
	DEVMETHOD(device_detach, viawd_detach),
	DEVMETHOD(device_shutdown, viawd_detach),
	{0,0}
};

static driver_t viawd_driver = {
	"viawd",
	viawd_methods,
	sizeof(struct viawd_softc),
};

DRIVER_MODULE(viawd, isab, viawd_driver, viawd_devclass, NULL, NULL);
