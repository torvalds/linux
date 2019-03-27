/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2002 by Peter Grehan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 *  PSIM 'iobus' local bus. Should be set up in the device tree like:
 *
 *     /iobus@0x80000000/name psim-iobus
 *
 *  Code borrowed from various nexus.c and uninorth.c :-)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/vmparam.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/resource.h>

#include <powerpc/psim/iobusvar.h>

struct iobus_softc {
	phandle_t     sc_node;
	vm_offset_t   sc_addr;
	vm_offset_t   sc_size;
	struct        rman sc_mem_rman;
};

static MALLOC_DEFINE(M_IOBUS, "iobus", "iobus device information");

static int  iobus_probe(device_t);
static int  iobus_attach(device_t);
static int  iobus_print_child(device_t dev, device_t child);
static void iobus_probe_nomatch(device_t, device_t);
static int  iobus_read_ivar(device_t, device_t, int, uintptr_t *);
static int  iobus_write_ivar(device_t, device_t, int, uintptr_t);
static struct   resource *iobus_alloc_resource(device_t, device_t, int, int *,
					       rman_res_t, rman_res_t, rman_res_t,
					       u_int);
static int  iobus_activate_resource(device_t, device_t, int, int,
				    struct resource *);
static int  iobus_deactivate_resource(device_t, device_t, int, int,
				      struct resource *);
static int  iobus_release_resource(device_t, device_t, int, int,
				   struct resource *);

/*
 * Bus interface definition
 */
static device_method_t iobus_methods[] = {
        /* Device interface */
        DEVMETHOD(device_probe,         iobus_probe),
        DEVMETHOD(device_attach,        iobus_attach),
        DEVMETHOD(device_detach,        bus_generic_detach),
        DEVMETHOD(device_shutdown,      bus_generic_shutdown),
        DEVMETHOD(device_suspend,       bus_generic_suspend),
        DEVMETHOD(device_resume,        bus_generic_resume),

        /* Bus interface */
        DEVMETHOD(bus_print_child,      iobus_print_child),
        DEVMETHOD(bus_probe_nomatch,    iobus_probe_nomatch),
        DEVMETHOD(bus_read_ivar,        iobus_read_ivar),
        DEVMETHOD(bus_write_ivar,       iobus_write_ivar),
        DEVMETHOD(bus_setup_intr,       bus_generic_setup_intr),
        DEVMETHOD(bus_teardown_intr,    bus_generic_teardown_intr),

        DEVMETHOD(bus_alloc_resource,   iobus_alloc_resource),
        DEVMETHOD(bus_release_resource, iobus_release_resource),
        DEVMETHOD(bus_activate_resource, iobus_activate_resource),
        DEVMETHOD(bus_deactivate_resource, iobus_deactivate_resource),

        { 0, 0 }
};

static driver_t iobus_driver = {
        "iobus",
        iobus_methods,
        sizeof(struct iobus_softc)
};

devclass_t iobus_devclass;

DRIVER_MODULE(iobus, ofwbus, iobus_driver, iobus_devclass, 0, 0);

static int
iobus_probe(device_t dev)
{
	const char *type = ofw_bus_get_name(dev);

	if (strcmp(type, "psim-iobus") != 0)
		return (ENXIO);

	device_set_desc(dev, "PSIM local bus");
	return (0);	
}

/*
 * Add interrupt/addr range to the dev's resource list if present
 */
static void
iobus_add_intr(phandle_t devnode, struct iobus_devinfo *dinfo)
{
	u_int intr = -1;

	if (OF_getprop(devnode, "interrupt", &intr, sizeof(intr)) != -1) {
		resource_list_add(&dinfo->id_resources, 
				  SYS_RES_IRQ, 0, intr, intr, 1);
	}
	dinfo->id_interrupt = intr;
}


static void
iobus_add_reg(phandle_t devnode, struct iobus_devinfo *dinfo,
	      vm_offset_t iobus_off)
{
	u_int size;
	int i;

	size = OF_getprop(devnode, "reg", dinfo->id_reg,sizeof(dinfo->id_reg));

	if (size != -1) {
		dinfo->id_nregs = size / (sizeof(dinfo->id_reg[0]));

		for (i = 0; i < dinfo->id_nregs; i+= 3) {
			/*
			 * Scale the absolute addresses back to iobus
			 * relative offsets. This is to better simulate
			 * macio
			 */
			dinfo->id_reg[i+1] -= iobus_off;

			resource_list_add(&dinfo->id_resources,
					  SYS_RES_MEMORY, 0,
					  dinfo->id_reg[i+1], 
					  dinfo->id_reg[i+1] + 
					      dinfo->id_reg[i+2],
					  dinfo->id_reg[i+2]);
		}
	}
}


static int
iobus_attach(device_t dev)
{
	struct iobus_softc *sc;
        struct iobus_devinfo *dinfo;
        phandle_t  root;
        phandle_t  child;
        device_t   cdev;
        char *name;
	u_int reg[2];
	int size;

	sc = device_get_softc(dev);
	sc->sc_node = ofw_bus_get_node(dev);

	/*
	 * Find the base addr/size of the iobus, and initialize the
	 * resource manager
	 */
	size = OF_getprop(sc->sc_node, "reg", reg, sizeof(reg));
	if (size == sizeof(reg)) {
		sc->sc_addr = reg[0];
		sc->sc_size = reg[1];
	} else {
		return (ENXIO);
	}

	sc->sc_mem_rman.rm_type = RMAN_ARRAY;
        sc->sc_mem_rman.rm_descr = "IOBus Device Memory";
        if (rman_init(&sc->sc_mem_rman) != 0) {
		device_printf(dev,
                    "failed to init mem range resources\n");
                return (ENXIO);
	}
	rman_manage_region(&sc->sc_mem_rman, 0, sc->sc_size);

        /*
         * Iterate through the sub-devices
         */
        root = sc->sc_node;

        for (child = OF_child(root); child != 0; child = OF_peer(child)) {
                OF_getprop_alloc(child, "name", (void **)&name);

                cdev = device_add_child(dev, NULL, -1);
                if (cdev != NULL) {
                        dinfo = malloc(sizeof(*dinfo), M_IOBUS, M_WAITOK);
			memset(dinfo, 0, sizeof(*dinfo));
			resource_list_init(&dinfo->id_resources);
                        dinfo->id_node = child;
                        dinfo->id_name = name;
			iobus_add_intr(child, dinfo);
			iobus_add_reg(child, dinfo, sc->sc_addr);
                        device_set_ivars(cdev, dinfo);
                } else {
                        OF_prop_free(name);
                }
        }

        return (bus_generic_attach(dev));
}


static int
iobus_print_child(device_t dev, device_t child)
{
        struct iobus_devinfo *dinfo;
        struct resource_list *rl;
        int retval = 0;
	
	dinfo = device_get_ivars(child);
        rl = &dinfo->id_resources;

	retval += bus_print_child_header(dev, child);
	
        retval += printf(" offset 0x%x", dinfo->id_reg[1]);
        retval += resource_list_print_type(rl, "irq", SYS_RES_IRQ, "%jd");
	
        retval += bus_print_child_footer(dev, child);

        return (retval);	
}


static void
iobus_probe_nomatch(device_t dev, device_t child)
{
}


static int
iobus_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
        struct iobus_devinfo *dinfo;

        if ((dinfo = device_get_ivars(child)) == NULL)
                return (ENOENT);

        switch (which) {
        case IOBUS_IVAR_NODE:
                *result = dinfo->id_node;
                break;
        case IOBUS_IVAR_NAME:
                *result = (uintptr_t)dinfo->id_name;
                break;
	case IOBUS_IVAR_NREGS:
		*result = dinfo->id_nregs;
		break;
	case IOBUS_IVAR_REGS:
		*result = (uintptr_t)dinfo->id_reg;
		break;
        default:
                return (ENOENT);
        }

        return (0);
}


static int
iobus_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{
        return (EINVAL);
}


static struct resource *
iobus_alloc_resource(device_t bus, device_t child, int type, int *rid,
		     rman_res_t start, rman_res_t end, rman_res_t count,
		     u_int flags)
{
	struct iobus_softc *sc;
	int  needactivate;
	struct  resource *rv;
	struct  rman *rm;

	sc = device_get_softc(bus);

	needactivate = flags & RF_ACTIVE;
	flags &= ~RF_ACTIVE;

	switch (type) {
	case SYS_RES_MEMORY:
	case SYS_RES_IOPORT:
		rm = &sc->sc_mem_rman;
		break;
	case SYS_RES_IRQ:
		return (bus_alloc_resource(bus, type, rid, start, end, count,
		    flags));
	default:
		device_printf(bus, "unknown resource request from %s\n",
		    device_get_nameunit(child));
		return (NULL);
	}

	rv = rman_reserve_resource(rm, start, end, count, flags, child);
	if (rv == NULL) {
		device_printf(bus, "failed to reserve resource for %s\n",
			      device_get_nameunit(child));
		return (NULL);
	}

	rman_set_rid(rv, *rid);

	if (needactivate) {
		if (bus_activate_resource(child, type, *rid, rv) != 0) {
                        device_printf(bus,
				      "failed to activate resource for %s\n",
				      device_get_nameunit(child));
			rman_release_resource(rv);
			return (NULL);
                }
        }

	return (rv);	
}


static int
iobus_release_resource(device_t bus, device_t child, int type, int rid,
		       struct resource *res)
{
	if (rman_get_flags(res) & RF_ACTIVE) {
		int error = bus_deactivate_resource(child, type, rid, res);
		if (error)
			return error;
	}

	return (rman_release_resource(res));
}


static int
iobus_activate_resource(device_t bus, device_t child, int type, int rid,
			   struct resource *res)
{
	struct iobus_softc *sc;
	void    *p;

	sc = device_get_softc(bus);

	if (type == SYS_RES_IRQ)
                return (bus_activate_resource(bus, type, rid, res));

	if ((type == SYS_RES_MEMORY) || (type == SYS_RES_IOPORT)) {
		p = pmap_mapdev((vm_offset_t)rman_get_start(res) + sc->sc_addr,
				(vm_size_t)rman_get_size(res));
		if (p == NULL)
			return (ENOMEM);
		rman_set_virtual(res, p);
		rman_set_bustag(res, &bs_le_tag);
		rman_set_bushandle(res, (u_long)p);
	}

	return (rman_activate_resource(res));
}


static int
iobus_deactivate_resource(device_t bus, device_t child, int type, int rid,
			  struct resource *res)
{
        /*
         * If this is a memory resource, unmap it.
         */
        if ((type == SYS_RES_MEMORY) || (type == SYS_RES_IOPORT)) {
		u_int32_t psize;

		psize = rman_get_size(res);
		pmap_unmapdev((vm_offset_t)rman_get_virtual(res), psize);
	}

	return (rman_deactivate_resource(res));
}
