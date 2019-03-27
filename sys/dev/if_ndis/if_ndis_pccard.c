/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ctype.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <net80211/ieee80211_var.h>

#include <compat/ndis/pe_var.h>
#include <compat/ndis/cfg_var.h>
#include <compat/ndis/resource_var.h>
#include <compat/ndis/ntoskrnl_var.h>
#include <compat/ndis/ndis_var.h>
#include <dev/if_ndis/if_ndisvar.h>

#include <dev/pccard/pccardvar.h>
#include "card_if.h"

MODULE_DEPEND(ndis, pccard, 1, 1, 1);

static int ndis_probe_pccard	(device_t);
static int ndis_attach_pccard	(device_t);
static int ndis_detach_pccard	(device_t);
static struct resource_list *ndis_get_resource_list
				(device_t, device_t);
static int ndis_devcompare	(interface_type,
				 struct ndis_pccard_type *, device_t);
extern int ndisdrv_modevent	(module_t, int, void *);
extern int ndis_attach		(device_t);
extern int ndis_shutdown	(device_t);
extern int ndis_detach		(device_t);
extern int ndis_suspend		(device_t);
extern int ndis_resume		(device_t);

extern unsigned char drv_data[];

static device_method_t ndis_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ndis_probe_pccard),
	DEVMETHOD(device_attach,	ndis_attach_pccard),
	DEVMETHOD(device_detach,	ndis_detach_pccard),
	DEVMETHOD(device_shutdown,	ndis_shutdown),
	DEVMETHOD(device_suspend,	ndis_suspend),
	DEVMETHOD(device_resume,	ndis_resume),

	/* Bus interface. */

	/*
	 * This is an awful kludge, but we need it becase pccard
	 * does not implement a bus_get_resource_list() method.
	 */

	DEVMETHOD(bus_get_resource_list, ndis_get_resource_list),

	{ 0, 0 }
};

static driver_t ndis_driver = {
	"ndis",
	ndis_methods,
	sizeof(struct ndis_softc)
};

static devclass_t ndis_devclass;

DRIVER_MODULE(ndis, pccard, ndis_driver, ndis_devclass, ndisdrv_modevent, 0);

static int
ndis_devcompare(bustype, t, dev)
	interface_type		bustype;
	struct ndis_pccard_type	*t;
	device_t		dev;
{
	const char		*prodstr, *vendstr;
	int			error;

	if (bustype != PCMCIABus)
		return(FALSE);

	error = pccard_get_product_str(dev, &prodstr);
	if (error)
		return(FALSE);
	error = pccard_get_vendor_str(dev, &vendstr);
	if (error)
		return(FALSE);

	while(t->ndis_name != NULL) {
		if (strcasecmp(vendstr, t->ndis_vid) == 0 &&
		    strcasecmp(prodstr, t->ndis_did) == 0) {
			device_set_desc(dev, t->ndis_name);
			return(TRUE);
		}
		t++;
	}

	return(FALSE);
}

/*
 * Probe for an NDIS device. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
ndis_probe_pccard(dev)
	device_t		dev;
{
	driver_object		*drv;
	struct drvdb_ent	*db;

	drv = windrv_lookup(0, "PCCARD Bus"); 
	if (drv == NULL)
		return(ENXIO);

	db = windrv_match((matchfuncptr)ndis_devcompare, dev);

	if (db != NULL) {
		/* Create PDO for this device instance */
		windrv_create_pdo(drv, dev);
		return(0);
	}

	return(ENXIO);
}

#define NDIS_AM_RID 3

static int
ndis_alloc_amem(struct ndis_softc *sc)
{
	int error, rid;

	rid = NDIS_AM_RID;
	sc->ndis_res_am = bus_alloc_resource_anywhere(sc->ndis_dev,
	    SYS_RES_MEMORY, &rid, 0x1000, RF_ACTIVE);

	if (sc->ndis_res_am == NULL) {
		device_printf(sc->ndis_dev,
		    "failed to allocate attribute memory\n");
		return(ENXIO);
	}
	sc->ndis_rescnt++;
	resource_list_add(&sc->ndis_rl, SYS_RES_MEMORY, rid,
	    rman_get_start(sc->ndis_res_am), rman_get_end(sc->ndis_res_am),
	    rman_get_size(sc->ndis_res_am));

	error = CARD_SET_MEMORY_OFFSET(device_get_parent(sc->ndis_dev),
	    sc->ndis_dev, rid, 0, NULL);

	if (error) {
		device_printf(sc->ndis_dev,
		    "CARD_SET_MEMORY_OFFSET() returned 0x%x\n", error);
		return(error);
	}

	error = CARD_SET_RES_FLAGS(device_get_parent(sc->ndis_dev),
	    sc->ndis_dev, SYS_RES_MEMORY, rid, PCCARD_A_MEM_ATTR);

	if (error) {
		device_printf(sc->ndis_dev,
		    "CARD_SET_RES_FLAGS() returned 0x%x\n", error);
		return(error);
	}

	sc->ndis_am_rid = rid;

	return(0);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
ndis_attach_pccard(dev)
	device_t		dev;
{
	struct ndis_softc	*sc;
	int			unit, error = 0, rid;
	struct ndis_pccard_type	*t;
	int			devidx = 0;
	const char		*prodstr, *vendstr;
	struct drvdb_ent	*db;

	sc = device_get_softc(dev);
	unit = device_get_unit(dev);
	sc->ndis_dev = dev;

	db = windrv_match((matchfuncptr)ndis_devcompare, dev);
	if (db == NULL)
		return (ENXIO);
	sc->ndis_dobj = db->windrv_object;
	sc->ndis_regvals = db->windrv_regvals;
	resource_list_init(&sc->ndis_rl);

	sc->ndis_io_rid = 0;
	sc->ndis_res_io = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
	    &sc->ndis_io_rid, RF_ACTIVE);
	if (sc->ndis_res_io == NULL) {
		device_printf(dev,
		    "couldn't map iospace\n");
		error = ENXIO;
		goto fail;
	}
	sc->ndis_rescnt++;
	resource_list_add(&sc->ndis_rl, SYS_RES_IOPORT, sc->ndis_io_rid,
	    rman_get_start(sc->ndis_res_io), rman_get_end(sc->ndis_res_io),
	    rman_get_size(sc->ndis_res_io));

	rid = 0;
	sc->ndis_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->ndis_irq == NULL) {
		device_printf(dev,
		    "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}
	sc->ndis_rescnt++;
	resource_list_add(&sc->ndis_rl, SYS_RES_IRQ, rid,
	    rman_get_start(sc->ndis_irq), rman_get_start(sc->ndis_irq), 1);

	sc->ndis_iftype = PCMCIABus;

	/* Figure out exactly which device we matched. */

	t = db->windrv_devlist;

	error = pccard_get_product_str(dev, &prodstr);
	if (error)
		return(error);
	error = pccard_get_vendor_str(dev, &vendstr);
	if (error)
		return(error);

	while(t->ndis_name != NULL) {
		if (strcasecmp(vendstr, t->ndis_vid) == 0 &&
		    strcasecmp(prodstr, t->ndis_did) == 0)
			break;
		t++;
		devidx++;
	}

	sc->ndis_devidx = devidx;

	error = ndis_alloc_amem(sc);
	if (error) {
		device_printf(dev, "failed to allocate attribute memory\n");
		goto fail;
	}

	error = ndis_attach(dev);

fail:
	return(error);
}

static int
ndis_detach_pccard(device_t dev)
{
	struct ndis_softc *sc = device_get_softc(dev);

	(void) ndis_detach(dev);

	if (sc->ndis_res_am != NULL)
		bus_release_resource(sc->ndis_dev, SYS_RES_MEMORY,
		    sc->ndis_am_rid, sc->ndis_res_am);
	resource_list_free(&sc->ndis_rl);

	return (0);
}

static struct resource_list *
ndis_get_resource_list(dev, child)
	device_t		dev;
	device_t		child;
{
	struct ndis_softc	*sc;

	sc = device_get_softc(dev);
	return (&sc->ndis_rl);
}
