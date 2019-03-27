/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Marius Strobl <marius@FreeBSD.org>
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
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/bus_common.h>
#include <machine/resource.h>

#include <sparc64/sbus/ofw_sbus.h>
#include <sparc64/sbus/sbusreg.h>
#include <sparc64/sbus/sbusvar.h>

struct lebuffer_devinfo {
	struct ofw_bus_devinfo	ldi_obdinfo;
	struct resource_list	ldi_rl;
};

static devclass_t lebuffer_devclass;

static device_probe_t lebuffer_probe;
static device_attach_t lebuffer_attach;
static device_detach_t lebuffer_detach;
static bus_print_child_t lebuffer_print_child;
static bus_probe_nomatch_t lebuffer_probe_nomatch;
static bus_get_resource_list_t lebuffer_get_resource_list;
static ofw_bus_get_devinfo_t lebuffer_get_devinfo;

static struct lebuffer_devinfo *lebuffer_setup_dinfo(device_t, phandle_t);
static void lebuffer_destroy_dinfo(struct lebuffer_devinfo *);
static int lebuffer_print_res(struct lebuffer_devinfo *);

static device_method_t lebuffer_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		lebuffer_probe),
	DEVMETHOD(device_attach,	lebuffer_attach),
	DEVMETHOD(device_detach,	lebuffer_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	lebuffer_print_child),
	DEVMETHOD(bus_probe_nomatch,	lebuffer_probe_nomatch),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_alloc_resource,	bus_generic_rl_alloc_resource),
	DEVMETHOD(bus_adjust_resource,	bus_generic_adjust_resource),
	DEVMETHOD(bus_release_resource, bus_generic_rl_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_get_resource_list, lebuffer_get_resource_list),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	lebuffer_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	{ 0, 0 }
};

DEFINE_CLASS_0(lebuffer, lebuffer_driver, lebuffer_methods, 1);
DRIVER_MODULE(lebuffer, sbus, lebuffer_driver, lebuffer_devclass, 0, 0);
MODULE_DEPEND(lebuffer, sbus, 1, 1, 1);
MODULE_VERSION(lebuffer, 1);

static int
lebuffer_probe(device_t dev)
{
	const char *name;

	name = ofw_bus_get_name(dev);
	if (strcmp(name, "lebuffer") == 0) {
		device_set_desc_copy(dev, name);
		return (0);
	}
	return (ENXIO);
}

static int
lebuffer_attach(device_t dev)
{
	struct lebuffer_devinfo *ldi;
	device_t cdev;
	phandle_t child;
	int children;

	children = 0;
	for (child = OF_child(ofw_bus_get_node(dev)); child != 0;
	    child = OF_peer(child)) {
		if ((ldi = lebuffer_setup_dinfo(dev, child)) == NULL)
			continue;
		if (children != 0) {
			device_printf(dev,
			    "<%s>: only one child per buffer supported\n",
			    ldi->ldi_obdinfo.obd_name);
			lebuffer_destroy_dinfo(ldi);
			continue;
		}
		if ((cdev = device_add_child(dev, NULL, -1)) == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
			    ldi->ldi_obdinfo.obd_name);
			lebuffer_destroy_dinfo(ldi);
			continue;
		}
		device_set_ivars(cdev, ldi);
		children++;
	}
	return (bus_generic_attach(dev));
}

static int
lebuffer_detach(device_t dev)
{
	device_t *children;
	int i, nchildren;

	bus_generic_detach(dev);
	if (device_get_children(dev, &children, &nchildren) == 0) {
		for (i = 0; i < nchildren; i++) {
			lebuffer_destroy_dinfo(device_get_ivars(children[i]));
			device_delete_child(dev, children[i]);
		}
		free(children, M_TEMP);
	}
	return (0);
}

static struct lebuffer_devinfo *
lebuffer_setup_dinfo(device_t dev, phandle_t node)
{
	struct lebuffer_devinfo *ldi;
	struct sbus_regs *reg;
	uint32_t base, iv, *intr;
	int i, nreg, nintr, slot, rslot;

	ldi = malloc(sizeof(*ldi), M_DEVBUF, M_WAITOK | M_ZERO);
	if (ofw_bus_gen_setup_devinfo(&ldi->ldi_obdinfo, node) != 0) {
		free(ldi, M_DEVBUF);
		return (NULL);
	}
	resource_list_init(&ldi->ldi_rl);
	slot = -1;
	nreg = OF_getprop_alloc_multi(node, "reg", sizeof(*reg), (void **)&reg);
	if (nreg == -1) {
		device_printf(dev, "<%s>: incomplete\n",
		    ldi->ldi_obdinfo.obd_name);
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
			    ldi->ldi_obdinfo.obd_name);
			OF_prop_free(reg);
			goto fail;
		}
		slot = rslot;

		resource_list_add(&ldi->ldi_rl, SYS_RES_MEMORY, i, base,
		    base + reg[i].sbr_size, reg[i].sbr_size);
	}
	OF_prop_free(reg);
	if (slot != sbus_get_slot(dev)) {
		device_printf(dev, "<%s>: parent and child slot do not match\n",
		    ldi->ldi_obdinfo.obd_name);
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
			iv |= sbus_get_ign(dev) << INTMAP_IGN_SHIFT;
			resource_list_add(&ldi->ldi_rl, SYS_RES_IRQ, i,
			    iv, iv, 1);
		}
		OF_prop_free(intr);
	}
	return (ldi);

 fail:
	lebuffer_destroy_dinfo(ldi);
	return (NULL);
}

static void
lebuffer_destroy_dinfo(struct lebuffer_devinfo *dinfo)
{

	resource_list_free(&dinfo->ldi_rl);
	ofw_bus_gen_destroy_devinfo(&dinfo->ldi_obdinfo);
	free(dinfo, M_DEVBUF);
}

static int
lebuffer_print_child(device_t dev, device_t child)
{
	int rv;

	rv = bus_print_child_header(dev, child);
	rv += lebuffer_print_res(device_get_ivars(child));
	rv += bus_print_child_footer(dev, child);
	return (rv);
}

static void
lebuffer_probe_nomatch(device_t dev, device_t child)
{
	const char *type;

	device_printf(dev, "<%s>", ofw_bus_get_name(child));
	lebuffer_print_res(device_get_ivars(child));
	type = ofw_bus_get_type(child);
	printf(" type %s (no driver attached)\n",
	    type != NULL ? type : "unknown");
}

static struct resource_list *
lebuffer_get_resource_list(device_t dev, device_t child)
{
	struct lebuffer_devinfo *ldi;

	ldi = device_get_ivars(child);
	return (&ldi->ldi_rl);
}

static const struct ofw_bus_devinfo *
lebuffer_get_devinfo(device_t bus, device_t child)
{
	struct lebuffer_devinfo *ldi;

	ldi = device_get_ivars(child);
	return (&ldi->ldi_obdinfo);
}

static int
lebuffer_print_res(struct lebuffer_devinfo *ldi)
{
	int rv;

	rv = 0;
	rv += resource_list_print_type(&ldi->ldi_rl, "mem", SYS_RES_MEMORY,
	    "%#jx");
	rv += resource_list_print_type(&ldi->ldi_rl, "irq", SYS_RES_IRQ, "%jd");
	return (rv);
}
