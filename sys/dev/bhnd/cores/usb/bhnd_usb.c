/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010, Aleksandr Rybalko <ray@ddteam.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

/*
 * Ported version of BroadCom USB core driver from ZRouter project
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>

#include <dev/bhnd/cores/pmu/bhnd_pmureg.h>

#include "bhnd_usbvar.h"

/****************************** Variables ************************************/
static const struct bhnd_device bhnd_usb_devs[] = {
	BHND_DEVICE(BCM,	USB20H,	"USB2.0 Host core",		NULL),
	BHND_DEVICE_END
};

/****************************** Prototypes ***********************************/

static int	bhnd_usb_attach(device_t);
static int	bhnd_usb_probe(device_t);
static device_t	bhnd_usb_add_child(device_t dev, u_int order, const char *name, 
		    int unit);
static int	bhnd_usb_print_all_resources(device_t dev);
static int	bhnd_usb_print_child(device_t bus, device_t child);

static struct resource *	bhnd_usb_alloc_resource(device_t bus,
				    device_t child, int type, int *rid,
				    rman_res_t start, rman_res_t end,
				    rman_res_t count, u_int flags);
static int			bhnd_usb_release_resource(device_t dev,
				    device_t child, int type, int rid,
				    struct resource *r);

static struct resource_list *	bhnd_usb_get_reslist(device_t dev,
				    device_t child);

static int
bhnd_usb_probe(device_t dev)
{
	const struct bhnd_device	*id;

	id = bhnd_device_lookup(dev, bhnd_usb_devs, sizeof(bhnd_usb_devs[0]));
	if (id == NULL)
		return (ENXIO);

	device_set_desc(dev, id->desc);
	return (BUS_PROBE_DEFAULT);
}

static int
bhnd_usb_attach(device_t dev)
{
	struct bhnd_usb_softc	*sc;
	int			 rid;
	uint32_t		 tmp;
	int			 tries, err;

	sc = device_get_softc(dev);

	bhnd_reset_hw(dev, 0, 0);

	/*
	 * Allocate the resources which the parent bus has already
	 * determined for us.
	 * XXX: There are few windows (usually 2), RID should be chip-specific
	 */
	rid = 0;
	sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->sc_mem == NULL) {
		BHND_ERROR_DEV(dev, "unable to allocate memory");
		return (ENXIO);
	}

	sc->sc_bt = rman_get_bustag(sc->sc_mem);
	sc->sc_bh = rman_get_bushandle(sc->sc_mem);
	sc->sc_maddr = rman_get_start(sc->sc_mem);
	sc->sc_msize = rman_get_size(sc->sc_mem);

	rid = 0;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, 
		RF_SHAREABLE | RF_ACTIVE);
	if (sc->sc_irq == NULL) {
		BHND_ERROR_DEV(dev, "unable to allocate IRQ");
		return (ENXIO);
	}

	sc->sc_irqn = rman_get_start(sc->sc_irq);

	sc->mem_rman.rm_start = sc->sc_maddr;
	sc->mem_rman.rm_end = sc->sc_maddr + sc->sc_msize - 1;
	sc->mem_rman.rm_type = RMAN_ARRAY;
	sc->mem_rman.rm_descr = "BHND USB core I/O memory addresses";
	if (rman_init(&sc->mem_rman) != 0 ||
	    rman_manage_region(&sc->mem_rman, sc->mem_rman.rm_start,
	    sc->mem_rman.rm_end) != 0) {
		panic("%s: sc->mem_rman", __func__);
	}

	/* TODO: macros for registers */
	bus_write_4(sc->sc_mem, 0x200, 0x7ff); 
	DELAY(100); 

#define	OHCI_CONTROL		0x04
	bus_write_4(sc->sc_mem, OHCI_CONTROL, 0);

	if ( bhnd_get_device(dev) == BHND_COREID_USB20H) {

		uint32_t rev = bhnd_get_hwrev(dev);
		BHND_INFO_DEV(dev, "USB HOST 2.0 setup for rev %d", rev);
		if (rev == 1/* ? == 2 */) {
			/* SiBa code */

			/* Change Flush control reg */
			tmp = bus_read_4(sc->sc_mem, 0x400) & ~0x8;
			bus_write_4(sc->sc_mem, 0x400, tmp);
			tmp = bus_read_4(sc->sc_mem, 0x400);
			BHND_DEBUG_DEV(dev, "USB20H fcr: 0x%x", tmp);

			/* Change Shim control reg */
			tmp = bus_read_4(sc->sc_mem, 0x304) & ~0x100;
			bus_write_4(sc->sc_mem, 0x304, tmp);
			tmp = bus_read_4(sc->sc_mem, 0x304);
			BHND_DEBUG_DEV(dev, "USB20H shim: 0x%x", tmp);
		} else if (rev >= 5) {
			/* BCMA code */
			err = bhnd_alloc_pmu(dev);
			if(err) {
				BHND_ERROR_DEV(dev, "can't alloc pmu: %d", err);
				return (err);
			}

			err = bhnd_request_ext_rsrc(dev, 1);
			if(err) {
				BHND_ERROR_DEV(dev, "can't req ext: %d", err);
				return (err);
			}
			/* Take out of resets */
			bus_write_4(sc->sc_mem, 0x200, 0x4ff);
			DELAY(25);
			bus_write_4(sc->sc_mem, 0x200, 0x6ff);
			DELAY(25);

			/* Make sure digital and AFE are locked in USB PHY */
			bus_write_4(sc->sc_mem, 0x524, 0x6b);
			DELAY(50);
			bus_read_4(sc->sc_mem, 0x524);
			DELAY(50);
			bus_write_4(sc->sc_mem, 0x524, 0xab);
			DELAY(50);
			bus_read_4(sc->sc_mem, 0x524);
			DELAY(50);
			bus_write_4(sc->sc_mem, 0x524, 0x2b);
			DELAY(50);
			bus_read_4(sc->sc_mem, 0x524);
			DELAY(50);
			bus_write_4(sc->sc_mem, 0x524, 0x10ab);
			DELAY(50);
			bus_read_4(sc->sc_mem, 0x524);

			tries = 10000;
			for (;;) {
				DELAY(10);
				tmp = bus_read_4(sc->sc_mem, 0x528);
				if (tmp & 0xc000)
					break;
				if (--tries != 0)
					continue;

				tmp = bus_read_4(sc->sc_mem, 0x528);
				BHND_ERROR_DEV(dev, "USB20H mdio_rddata 0x%08x", tmp);
			}

			/* XXX: Puzzle code */
			bus_write_4(sc->sc_mem, 0x528, 0x80000000);
			bus_read_4(sc->sc_mem, 0x314);
			DELAY(265);
			bus_write_4(sc->sc_mem, 0x200, 0x7ff);
			DELAY(10);

			/* Take USB and HSIC out of non-driving modes */
			bus_write_4(sc->sc_mem, 0x510, 0);
		}
	}

	bus_generic_probe(dev);

	if (bhnd_get_device(dev) == BHND_COREID_USB20H &&
	    ( bhnd_get_hwrev(dev) > 0))
		bhnd_usb_add_child(dev, 0, "ehci", -1);
	bhnd_usb_add_child(dev, 1, "ohci", -1);

	bus_generic_attach(dev);

	return (0);
}

static struct resource *
bhnd_usb_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource			*rv;
	struct resource_list		*rl;
	struct resource_list_entry	*rle;
	int				 passthrough, isdefault, needactivate;
	struct bhnd_usb_softc		*sc = device_get_softc(bus);

	isdefault = RMAN_IS_DEFAULT_RANGE(start,end);
	passthrough = (device_get_parent(child) != bus);
	needactivate = flags & RF_ACTIVE;
	rle = NULL;

	if (!passthrough && isdefault) {
		BHND_INFO_DEV(bus, "trying allocate def %d - %d for %s", type,
		    *rid, device_get_nameunit(child) );

		rl = BUS_GET_RESOURCE_LIST(bus, child);
		rle = resource_list_find(rl, type, *rid);
		if (rle == NULL)
			return (NULL);
		if (rle->res != NULL)
			panic("%s: resource entry is busy", __func__);
		start = rle->start;
		end = rle->end;
		count = rle->count;
	} else {
		BHND_INFO_DEV(bus, "trying allocate %d - %d (%jx-%jx) for %s", type,
		   *rid, start, end, device_get_nameunit(child) );
	}

	/*
	 * If the request is for a resource which we manage,
	 * attempt to satisfy the allocation ourselves.
	 */
	if (type == SYS_RES_MEMORY) {

		rv = rman_reserve_resource(&sc->mem_rman, start, end, count,
		    flags, child);
		if (rv == NULL) {
			BHND_ERROR_DEV(bus, "could not reserve resource");
			return (0);
		}

		rman_set_rid(rv, *rid);

		if (needactivate &&
		    bus_activate_resource(child, type, *rid, rv)) {
			BHND_ERROR_DEV(bus, "could not activate resource");
			rman_release_resource(rv);
			return (0);
		}

		return (rv);
	}

	/*
	 * Pass the request to the parent.
	 */
	return (bus_generic_rl_alloc_resource(bus, child, type, rid, start, end,
	    count, flags));
}

static struct resource_list *
bhnd_usb_get_reslist(device_t dev, device_t child)
{
	struct bhnd_usb_devinfo	*sdi;

	sdi = device_get_ivars(child);

	return (&sdi->sdi_rl);
}

static int
bhnd_usb_release_resource(device_t dev, device_t child, int type,
    int rid, struct resource *r)
{
	struct bhnd_usb_softc		*sc;
	struct resource_list_entry	*rle;
	bool				 passthrough;
	int				 error;

	sc = device_get_softc(dev);
	passthrough = (device_get_parent(child) != dev);

	/* Delegate to our parent device's bus if the requested resource type
	 * isn't handled locally. */
	if (type != SYS_RES_MEMORY) {
		return (bus_generic_rl_release_resource(dev, child, type, rid,
		    r));
	}

	/* Deactivate resources */
	if (rman_get_flags(r) & RF_ACTIVE) {
		error = BUS_DEACTIVATE_RESOURCE(dev, child, type, rid, r);
		if (error)
			return (error);
	}

	if ((error = rman_release_resource(r)))
		return (error);

	if (!passthrough) {
		/* Clean resource list entry */
		rle = resource_list_find(BUS_GET_RESOURCE_LIST(dev, child),
		    type, rid);
		if (rle != NULL)
			rle->res = NULL;
	}

	return (0);
}

static int
bhnd_usb_print_all_resources(device_t dev)
{
	struct bhnd_usb_devinfo	*sdi;
	struct resource_list	*rl;
	int			 retval;

	retval = 0;
	sdi = device_get_ivars(dev);
	rl = &sdi->sdi_rl;

	if (STAILQ_FIRST(rl))
		retval += printf(" at");

	retval += resource_list_print_type(rl, "mem", SYS_RES_MEMORY, "%jx");
	retval += resource_list_print_type(rl, "irq", SYS_RES_IRQ, "%ld");

	return (retval);
}

static int
bhnd_usb_print_child(device_t bus, device_t child)
{
	int retval = 0;

	retval += bus_print_child_header(bus, child);
	retval += bhnd_usb_print_all_resources(child);
	if (device_get_flags(child))
		retval += printf(" flags %#x", device_get_flags(child));
	retval += printf(" on %s\n", device_get_nameunit(bus));

	return (retval);
}

static device_t
bhnd_usb_add_child(device_t dev, u_int order, const char *name, int unit)
{
	struct bhnd_usb_softc		*sc;
	struct bhnd_usb_devinfo 	*sdi;
	device_t 			 child;
	int				 error;

	sc = device_get_softc(dev);

	sdi = malloc(sizeof(struct bhnd_usb_devinfo), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (sdi == NULL)
		return (NULL);

	resource_list_init(&sdi->sdi_rl);
	sdi->sdi_irq_mapped = false;

	if (strncmp(name, "ohci", 4) == 0) 
	{
		sdi->sdi_maddr = sc->sc_maddr + 0x000;
		sdi->sdi_msize = 0x200;
	}
	else if (strncmp(name, "ehci", 4) == 0) 
	{
		sdi->sdi_maddr = sc->sc_maddr + 0x000;
		sdi->sdi_msize = 0x1000;
	}
	else
	{
		panic("Unknown subdevice");
	}

	/* Map the child's IRQ */
	if ((error = bhnd_map_intr(dev, 0, &sdi->sdi_irq))) {
		BHND_ERROR_DEV(dev, "could not map %s interrupt: %d", name,
		    error);
		goto failed;
	}
	sdi->sdi_irq_mapped = true;

	BHND_INFO_DEV(dev, "%s: irq=%ju maddr=0x%jx", name, sdi->sdi_irq,
	    sdi->sdi_maddr);

	/*
	 * Add memory window and irq to child's resource list.
	 */
	resource_list_add(&sdi->sdi_rl, SYS_RES_MEMORY, 0, sdi->sdi_maddr,
	    sdi->sdi_maddr + sdi->sdi_msize - 1, sdi->sdi_msize);

	resource_list_add(&sdi->sdi_rl, SYS_RES_IRQ, 0, sdi->sdi_irq,
	    sdi->sdi_irq, 1);

	child = device_add_child_ordered(dev, order, name, unit);
	if (child == NULL) {
		BHND_ERROR_DEV(dev, "could not add %s", name);
		goto failed;
	}

	device_set_ivars(child, sdi);
	return (child);

failed:
	if (sdi->sdi_irq_mapped)
		bhnd_unmap_intr(dev, sdi->sdi_irq);

	resource_list_free(&sdi->sdi_rl);

	free(sdi, M_DEVBUF);
	return (NULL);
}

static void
bhnd_usb_child_deleted(device_t dev, device_t child)
{
	struct bhnd_usb_devinfo	*dinfo;

	if ((dinfo = device_get_ivars(child)) == NULL)
		return;

	if (dinfo->sdi_irq_mapped)
		bhnd_unmap_intr(dev, dinfo->sdi_irq);

	resource_list_free(&dinfo->sdi_rl);
	free(dinfo, M_DEVBUF);
}

static device_method_t bhnd_usb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_attach,		bhnd_usb_attach),
	DEVMETHOD(device_probe,			bhnd_usb_probe),

	/* Bus interface */
	DEVMETHOD(bus_add_child,		bhnd_usb_add_child),
	DEVMETHOD(bus_child_deleted,		bhnd_usb_child_deleted),
	DEVMETHOD(bus_alloc_resource,		bhnd_usb_alloc_resource),
	DEVMETHOD(bus_get_resource_list,	bhnd_usb_get_reslist),
	DEVMETHOD(bus_print_child,		bhnd_usb_print_child),
	DEVMETHOD(bus_release_resource,		bhnd_usb_release_resource),
	/* Bus interface: generic part */
	DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,		bus_generic_teardown_intr),

	DEVMETHOD_END
};

static devclass_t bhnd_usb_devclass;

DEFINE_CLASS_0(bhnd_usb, bhnd_usb_driver, bhnd_usb_methods,
    sizeof(struct bhnd_usb_softc));
DRIVER_MODULE(bhnd_usb, bhnd, bhnd_usb_driver, bhnd_usb_devclass, 0, 0);

MODULE_VERSION(bhnd_usb, 1);
