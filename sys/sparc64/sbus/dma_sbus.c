/*	$OpenBSD: dma_sbus.c,v 1.16 2008/06/26 05:42:18 ray Exp $	*/
/*	$NetBSD: dma_sbus.c,v 1.32 2008/04/28 20:23:57 martin Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1994 Peter Galbavy.  All rights reserved.
 * Copyright (c) 2005 Marius Strobl <marius@FreeBSD.org>. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/bus_common.h>
#include <machine/resource.h>

#include <sparc64/sbus/lsi64854reg.h>
#include <sparc64/sbus/lsi64854var.h>
#include <sparc64/sbus/ofw_sbus.h>
#include <sparc64/sbus/sbusreg.h>
#include <sparc64/sbus/sbusvar.h>

struct dma_devinfo {
	struct ofw_bus_devinfo	ddi_obdinfo;
	struct resource_list	ddi_rl;
};

struct dma_softc {
	struct lsi64854_softc	sc_lsi64854;	/* base device */
	int			sc_ign;
	int			sc_slot;
};

static devclass_t dma_devclass;

static device_probe_t dma_probe;
static device_attach_t dma_attach;
static bus_print_child_t dma_print_child;
static bus_probe_nomatch_t dma_probe_nomatch;
static bus_get_resource_list_t dma_get_resource_list;
static ofw_bus_get_devinfo_t dma_get_devinfo;

static struct dma_devinfo *dma_setup_dinfo(device_t, struct dma_softc *,
    phandle_t);
static void dma_destroy_dinfo(struct dma_devinfo *);
static int dma_print_res(struct dma_devinfo *);

static device_method_t dma_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		dma_probe),
	DEVMETHOD(device_attach,	dma_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	dma_print_child),
	DEVMETHOD(bus_probe_nomatch,	dma_probe_nomatch),
	DEVMETHOD(bus_alloc_resource,	bus_generic_rl_alloc_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,	bus_generic_adjust_resource),
	DEVMETHOD(bus_release_resource, bus_generic_rl_release_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_get_resource_list, dma_get_resource_list),
	DEVMETHOD(bus_child_pnpinfo_str, ofw_bus_gen_child_pnpinfo_str),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	dma_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static driver_t dma_driver = {
	"dma",
	dma_methods,
	sizeof(struct dma_softc),
};

/*
 * The probe order is handled by sbus(4) as we don't want the variants
 * with children to be attached earlier than the stand-alone controllers
 * in order to generally preserve the OFW device tree order.
 */
EARLY_DRIVER_MODULE(dma, sbus, dma_driver, dma_devclass, 0, 0,
    BUS_PASS_DEFAULT);
MODULE_DEPEND(dma, sbus, 1, 1, 1);
MODULE_VERSION(dma, 1);

static int
dma_probe(device_t dev)
{
	const char *name;

	name = ofw_bus_get_name(dev);
	if (strcmp(name, "espdma") == 0 || strcmp(name, "dma") == 0 ||
	    strcmp(name, "ledma") == 0) {
		device_set_desc_copy(dev, name);
		return (0);
	}
	return (ENXIO);
}

static int
dma_attach(device_t dev)
{
	struct dma_softc *dsc;
	struct lsi64854_softc *lsc;
	struct dma_devinfo *ddi;
	device_t cdev;
	const char *name;
	char *cabletype;
	uint32_t csr;
	phandle_t child, node;
	int error, i;

	dsc = device_get_softc(dev);
	lsc = &dsc->sc_lsi64854;

	name = ofw_bus_get_name(dev);
	node = ofw_bus_get_node(dev);
	dsc->sc_ign = sbus_get_ign(dev);
	dsc->sc_slot = sbus_get_slot(dev);

	i = 0;
	lsc->sc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &i,
	    RF_ACTIVE);
	if (lsc->sc_res == NULL) {
		device_printf(dev, "cannot allocate resources\n");
		return (ENXIO);
	}

	if (strcmp(name, "espdma") == 0 || strcmp(name, "dma") == 0)
		lsc->sc_channel = L64854_CHANNEL_SCSI;
	else if (strcmp(name, "ledma") == 0) {
		/*
		 * Check to see which cable type is currently active and
		 * set the appropriate bit in the ledma csr so that it
		 * gets used. If we didn't netboot, the PROM won't have
		 * the "cable-selection" property; default to TP and then
		 * the user can change it via a "media" option to ifconfig.
		 */
		csr = L64854_GCSR(lsc);
		if ((OF_getprop_alloc(node, "cable-selection",
		    (void **)&cabletype)) == -1) {
			/* assume TP if nothing there */
			csr |= E_TP_AUI;
		} else {
			if (strcmp(cabletype, "aui") == 0)
				csr &= ~E_TP_AUI;
			else
				csr |= E_TP_AUI;
			OF_prop_free(cabletype);
		}
		L64854_SCSR(lsc, csr);
		DELAY(20000);	/* manual says we need a 20ms delay */
		lsc->sc_channel = L64854_CHANNEL_ENET;
	} else {
		device_printf(dev, "unsupported DMA channel\n");
		error = ENXIO;
		goto fail_lres;
	}

	error = bus_dma_tag_create(
	    bus_get_dma_tag(dev),	/* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE,		/* maxsize */
	    BUS_SPACE_UNRESTRICTED,	/* nsegments */
	    BUS_SPACE_MAXSIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* no locking */
	    &lsc->sc_parent_dmat);
	if (error != 0) {
		device_printf(dev, "cannot allocate parent DMA tag\n");
		goto fail_lres;
	}

	i = sbus_get_burstsz(dev);
	lsc->sc_burst = (i & SBUS_BURST_32) ? 32 :
	    (i & SBUS_BURST_16) ? 16 : 0;
	lsc->sc_dev = dev;

	/* Attach children. */
	i = 0;
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if ((ddi = dma_setup_dinfo(dev, dsc, child)) == NULL)
			continue;
		if (i != 0) {
			device_printf(dev,
			    "<%s>: only one child per DMA channel supported\n",
			    ddi->ddi_obdinfo.obd_name);
			dma_destroy_dinfo(ddi);
			continue;
		}
		if ((cdev = device_add_child(dev, NULL, -1)) == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
			    ddi->ddi_obdinfo.obd_name);
			dma_destroy_dinfo(ddi);
			continue;
		}
		device_set_ivars(cdev, ddi);
		i++;
	}
	return (bus_generic_attach(dev));

 fail_lres:
	bus_release_resource(dev, SYS_RES_MEMORY, rman_get_rid(lsc->sc_res),
	    lsc->sc_res);
	return (error);
}

static struct dma_devinfo *
dma_setup_dinfo(device_t dev, struct dma_softc *dsc, phandle_t node)
{
	struct dma_devinfo *ddi;
	struct sbus_regs *reg;
	uint32_t base, iv, *intr;
	int i, nreg, nintr, slot, rslot;

	ddi = malloc(sizeof(*ddi), M_DEVBUF, M_WAITOK | M_ZERO);
	if (ofw_bus_gen_setup_devinfo(&ddi->ddi_obdinfo, node) != 0) {
		free(ddi, M_DEVBUF);
		return (NULL);
	}
	resource_list_init(&ddi->ddi_rl);
	slot = -1;
	nreg = OF_getprop_alloc_multi(node, "reg", sizeof(*reg), (void **)&reg);
	if (nreg == -1) {
		device_printf(dev, "<%s>: incomplete\n",
		    ddi->ddi_obdinfo.obd_name);
		goto fail;
	}
	for (i = 0; i < nreg; i++) {
		base = reg[i].sbr_offset;
		if (SBUS_ABS(base)) {
			rslot = SBUS_ABS_TO_SLOT(base);
			base = SBUS_ABS_TO_OFFSET(base);
		} else
			rslot = reg[i].sbr_slot;
		if (slot != -1 && slot != rslot) {
			device_printf(dev, "<%s>: multiple slots\n",
			    ddi->ddi_obdinfo.obd_name);
			OF_prop_free(reg);
			goto fail;
		}
		slot = rslot;

		resource_list_add(&ddi->ddi_rl, SYS_RES_MEMORY, i, base,
		    base + reg[i].sbr_size, reg[i].sbr_size);
	}
	OF_prop_free(reg);
	if (slot != dsc->sc_slot) {
		device_printf(dev, "<%s>: parent and child slot do not match\n",
		    ddi->ddi_obdinfo.obd_name);
		goto fail;
	}

	/*
	 * The `interrupts' property contains the SBus interrupt level.
	 */
	nintr = OF_getprop_alloc_multi(node, "interrupts", sizeof(*intr),
	    (void **)&intr);
	if (nintr != -1) {
		for (i = 0; i < nintr; i++) {
			iv = intr[i];
			/*
			 * SBus card devices need the slot number encoded into
			 * the vector as this is generally not done.
			 */
			if ((iv & INTMAP_OBIO_MASK) == 0)
				iv |= slot << 3;
			/* Set the IGN as appropriate. */
			iv |= dsc->sc_ign << INTMAP_IGN_SHIFT;
			resource_list_add(&ddi->ddi_rl, SYS_RES_IRQ, i,
			    iv, iv, 1);
		}
		OF_prop_free(intr);
	}
	return (ddi);

 fail:
	dma_destroy_dinfo(ddi);
	return (NULL);
}

static void
dma_destroy_dinfo(struct dma_devinfo *dinfo)
{

	resource_list_free(&dinfo->ddi_rl);
	ofw_bus_gen_destroy_devinfo(&dinfo->ddi_obdinfo);
	free(dinfo, M_DEVBUF);
}

static int
dma_print_child(device_t dev, device_t child)
{
	int rv;

	rv = bus_print_child_header(dev, child);
	rv += dma_print_res(device_get_ivars(child));
	rv += bus_print_child_footer(dev, child);
	return (rv);
}

static void
dma_probe_nomatch(device_t dev, device_t child)
{
	const char *type;

	device_printf(dev, "<%s>", ofw_bus_get_name(child));
	dma_print_res(device_get_ivars(child));
	type = ofw_bus_get_type(child);
	printf(" type %s (no driver attached)\n",
	    type != NULL ? type : "unknown");
}

static struct resource_list *
dma_get_resource_list(device_t dev, device_t child)
{
	struct dma_devinfo *ddi;

	ddi = device_get_ivars(child);
	return (&ddi->ddi_rl);
}

static const struct ofw_bus_devinfo *
dma_get_devinfo(device_t bus, device_t child)
{
	struct dma_devinfo *ddi;

	ddi = device_get_ivars(child);
	return (&ddi->ddi_obdinfo);
}

static int
dma_print_res(struct dma_devinfo *ddi)
{
	int rv;

	rv = 0;
	rv += resource_list_print_type(&ddi->ddi_rl, "mem", SYS_RES_MEMORY,
	    "%#jx");
	rv += resource_list_print_type(&ddi->ddi_rl, "irq", SYS_RES_IRQ, "%jd");
	return (rv);
}
