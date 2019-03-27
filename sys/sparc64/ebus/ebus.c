/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-3-Clause
 *
 * Copyright (c) 1999, 2000 Matthew R. Green
 * Copyright (c) 2009 by Marius Strobl <marius@FreeBSD.org>
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
 *	from: NetBSD: ebus.c,v 1.52 2008/05/29 14:51:26 mrg Exp
 */
/*-
 * Copyright (c) 2001 Thomas Moestl <tmm@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for JBus to EBus and PCI to EBus bridges
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#ifndef SUN4V
#include <machine/bus_common.h>
#endif
#include <machine/intr_machdep.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <sparc64/pci/ofw_pci.h>

/*
 * The register, interrupt map and for the PCI variant also the ranges
 * properties are identical to the ISA ones.
 */
#include <sparc64/isa/ofw_isa.h>

struct ebus_nexus_ranges {
	uint32_t	child_hi;
	uint32_t	child_lo;
	uint32_t	phys_hi;
	uint32_t	phys_lo;
	uint32_t	size;
};

struct ebus_devinfo {
	struct ofw_bus_devinfo	edi_obdinfo;
	struct resource_list	edi_rl;
};

struct ebus_rinfo {
	int			eri_rtype;
	struct rman		eri_rman;
	struct resource		*eri_res;
};

struct ebus_softc {
	void			*sc_range;
	struct ebus_rinfo	*sc_rinfo;

	u_int			sc_flags;
#define	EBUS_PCI		(1 << 0)

	int			sc_nrange;

	struct ofw_bus_iinfo	sc_iinfo;

#ifndef SUN4V
	uint32_t		sc_ign;
#endif
};

static device_probe_t ebus_nexus_probe;
static device_attach_t ebus_nexus_attach;
static device_probe_t ebus_pci_probe;
static device_attach_t ebus_pci_attach;
static bus_print_child_t ebus_print_child;
static bus_probe_nomatch_t ebus_probe_nomatch;
static bus_alloc_resource_t ebus_alloc_resource;
static bus_activate_resource_t ebus_activate_resource;
static bus_adjust_resource_t ebus_adjust_resource;
static bus_release_resource_t ebus_release_resource;
static bus_setup_intr_t ebus_setup_intr;
static bus_get_resource_list_t ebus_get_resource_list;
static ofw_bus_get_devinfo_t ebus_get_devinfo;

static int ebus_attach(device_t dev, struct ebus_softc *sc, phandle_t node);
static struct ebus_devinfo *ebus_setup_dinfo(device_t dev,
    struct ebus_softc *sc, phandle_t node);
static void ebus_destroy_dinfo(struct ebus_devinfo *edi);
static int ebus_print_res(struct ebus_devinfo *edi);

static devclass_t ebus_devclass;

static device_method_t ebus_nexus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ebus_nexus_probe),
	DEVMETHOD(device_attach,	ebus_nexus_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	ebus_print_child),
	DEVMETHOD(bus_probe_nomatch,	ebus_probe_nomatch),
	DEVMETHOD(bus_alloc_resource,	ebus_alloc_resource),
	DEVMETHOD(bus_activate_resource, ebus_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,	ebus_adjust_resource),
	DEVMETHOD(bus_release_resource,	ebus_release_resource),
	DEVMETHOD(bus_setup_intr,	ebus_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_get_resource_list, ebus_get_resource_list),
	DEVMETHOD(bus_child_pnpinfo_str, ofw_bus_gen_child_pnpinfo_str),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	ebus_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static driver_t ebus_nexus_driver = {
	"ebus",
	ebus_nexus_methods,
	sizeof(struct ebus_softc),
};

/*
 * NB: we rely on the interrupt controllers of the accompanying PCI-Express
 * bridge to be registered as the nexus variant of the EBus bridges doesn't
 * employ its own one.
 */
EARLY_DRIVER_MODULE(ebus, nexus, ebus_nexus_driver, ebus_devclass, 0, 0,
    BUS_PASS_BUS + 1);
MODULE_DEPEND(ebus, nexus, 1, 1, 1);

static device_method_t ebus_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ebus_pci_probe),
	DEVMETHOD(device_attach,	ebus_pci_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	ebus_print_child),
	DEVMETHOD(bus_probe_nomatch,	ebus_probe_nomatch),
	DEVMETHOD(bus_alloc_resource,	ebus_alloc_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_release_resource,	ebus_release_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_get_resource_list, ebus_get_resource_list),
	DEVMETHOD(bus_child_pnpinfo_str, ofw_bus_gen_child_pnpinfo_str),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	ebus_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static driver_t ebus_pci_driver = {
	"ebus",
	ebus_pci_methods,
	sizeof(struct ebus_softc),
};

EARLY_DRIVER_MODULE(ebus, pci, ebus_pci_driver, ebus_devclass, 0, 0,
    BUS_PASS_BUS);
MODULE_DEPEND(ebus, pci, 1, 1, 1);
MODULE_VERSION(ebus, 1);

static int
ebus_nexus_probe(device_t dev)
{
	const char* compat;

	compat = ofw_bus_get_compat(dev);
	if (compat != NULL && strcmp(ofw_bus_get_name(dev), "ebus") == 0 &&
	    strcmp(compat, "jbus-ebus") == 0) {
		device_set_desc(dev, "JBus-EBus bridge");
		return (BUS_PROBE_GENERIC);
	}
	return (ENXIO);
}

static int
ebus_pci_probe(device_t dev)
{

	if (pci_get_class(dev) != PCIC_BRIDGE ||
	    pci_get_vendor(dev) != 0x108e ||
	    strcmp(ofw_bus_get_name(dev), "ebus") != 0)
		return (ENXIO);

	if (pci_get_device(dev) == 0x1000)
		device_set_desc(dev, "PCI-EBus2 bridge");
	else if (pci_get_device(dev) == 0x1100)
		device_set_desc(dev, "PCI-EBus3 bridge");
	else
		return (ENXIO);
	return (BUS_PROBE_GENERIC);
}

static int
ebus_nexus_attach(device_t dev)
{
	struct ebus_softc *sc;
	phandle_t node;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

#ifndef SUN4V
	if (OF_getprop(node, "portid", &sc->sc_ign,
	    sizeof(sc->sc_ign)) == -1) {
		device_printf(dev, "could not determine IGN");
		return (ENXIO);
	}
#endif

	sc->sc_nrange = OF_getprop_alloc_multi(node, "ranges",
	    sizeof(struct ebus_nexus_ranges), &sc->sc_range);
	if (sc->sc_nrange == -1) {
		device_printf(dev, "could not get ranges property\n");
		return (ENXIO);
	}
	return (ebus_attach(dev, sc, node));
}

static int
ebus_pci_attach(device_t dev)
{
	struct ebus_softc *sc;
	struct ebus_rinfo *eri;
	struct resource *res;
	struct isa_ranges *range;
	phandle_t node;
	int i, rnum, rid;

	sc = device_get_softc(dev);
	sc->sc_flags |= EBUS_PCI;

	pci_write_config(dev, PCIR_COMMAND,
	    pci_read_config(dev, PCIR_COMMAND, 2) | PCIM_CMD_SERRESPEN |
	    PCIM_CMD_PERRESPEN | PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN, 2);
	pci_write_config(dev, PCIR_CACHELNSZ, 16 /* 64 bytes */, 1);
	pci_write_config(dev, PCIR_LATTIMER, 64 /* 64 PCI cycles */, 1);

	node = ofw_bus_get_node(dev);
	sc->sc_nrange = OF_getprop_alloc_multi(node, "ranges",
	    sizeof(struct isa_ranges), &sc->sc_range);
	if (sc->sc_nrange == -1) {
		device_printf(dev, "could not get ranges property\n");
		return (ENXIO);
	}

	sc->sc_rinfo = malloc(sizeof(*sc->sc_rinfo) * sc->sc_nrange, M_DEVBUF,
	    M_WAITOK | M_ZERO);

	/* For every range, there must be a matching resource. */
	for (rnum = 0; rnum < sc->sc_nrange; rnum++) {
		eri = &sc->sc_rinfo[rnum];
		range = &((struct isa_ranges *)sc->sc_range)[rnum];
		eri->eri_rtype = ofw_isa_range_restype(range);
		rid = PCIR_BAR(rnum);
		res = bus_alloc_resource_any(dev, eri->eri_rtype, &rid,
		    RF_ACTIVE);
		if (res == NULL) {
			device_printf(dev,
			    "could not allocate range resource %d\n", rnum);
			goto fail;
		}
		if (rman_get_start(res) != ISA_RANGE_PHYS(range)) {
			device_printf(dev,
			    "mismatch in start of range %d (0x%lx/0x%lx)\n",
			    rnum, rman_get_start(res), ISA_RANGE_PHYS(range));
			goto fail;
		}
		if (rman_get_size(res) != range->size) {
			device_printf(dev,
			    "mismatch in size of range %d (0x%lx/0x%x)\n",
			    rnum, rman_get_size(res), range->size);
			goto fail;
		}
		eri->eri_res = res;
		eri->eri_rman.rm_type = RMAN_ARRAY;
		eri->eri_rman.rm_descr = "EBus range";
		if (rman_init_from_resource(&eri->eri_rman, res) != 0) {
			device_printf(dev,
			    "could not initialize rman for range %d", rnum);
			goto fail;
		}
	}
	return (ebus_attach(dev, sc, node));

 fail:
	for (i = rnum; i >= 0; i--) {
		eri = &sc->sc_rinfo[i];
		if (i < rnum)
			rman_fini(&eri->eri_rman);
		if (eri->eri_res != NULL) {
			bus_release_resource(dev, eri->eri_rtype,
			    PCIR_BAR(rnum), eri->eri_res);
		}
	}
	free(sc->sc_rinfo, M_DEVBUF);
	OF_prop_free(sc->sc_range);
	return (ENXIO);
}

static int
ebus_attach(device_t dev, struct ebus_softc *sc, phandle_t node)
{
	struct ebus_devinfo *edi;
	device_t cdev;

	ofw_bus_setup_iinfo(node, &sc->sc_iinfo, sizeof(ofw_isa_intr_t));

	/*
	 * Now attach our children.
	 */
	for (node = OF_child(node); node > 0; node = OF_peer(node)) {
		if ((edi = ebus_setup_dinfo(dev, sc, node)) == NULL)
			continue;
		if ((cdev = device_add_child(dev, NULL, -1)) == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
			    edi->edi_obdinfo.obd_name);
			ebus_destroy_dinfo(edi);
			continue;
		}
		device_set_ivars(cdev, edi);
	}
	return (bus_generic_attach(dev));
}

static int
ebus_print_child(device_t dev, device_t child)
{
	int retval;

	retval = bus_print_child_header(dev, child);
	retval += ebus_print_res(device_get_ivars(child));
	retval += bus_print_child_footer(dev, child);
	return (retval);
}

static void
ebus_probe_nomatch(device_t dev, device_t child)
{

	device_printf(dev, "<%s>", ofw_bus_get_name(child));
	ebus_print_res(device_get_ivars(child));
	printf(" (no driver attached)\n");
}

static struct resource *
ebus_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct ebus_softc *sc;
	struct resource_list *rl;
	struct resource_list_entry *rle = NULL;
	struct resource *res;
	struct ebus_rinfo *eri;
	struct ebus_nexus_ranges *enr;
	uint64_t cend, cstart, offset;
	int i, isdefault, passthrough, ridx;

	isdefault = RMAN_IS_DEFAULT_RANGE(start, end);
	passthrough = (device_get_parent(child) != bus);
	sc = device_get_softc(bus);
	rl = BUS_GET_RESOURCE_LIST(bus, child);
	switch (type) {
	case SYS_RES_MEMORY:
		KASSERT(!(isdefault && passthrough),
		    ("%s: passthrough of default allocation", __func__));
		if (!passthrough) {
			rle = resource_list_find(rl, type, *rid);
			if (rle == NULL)
				return (NULL);
			KASSERT(rle->res == NULL,
			    ("%s: resource entry is busy", __func__));
			if (isdefault) {
				start = rle->start;
				count = ulmax(count, rle->count);
				end = ulmax(rle->end, start + count - 1);
			}
		}

		res = NULL;
		if ((sc->sc_flags & EBUS_PCI) != 0) {
			/*
			 * Map EBus ranges to PCI ranges.  This may include
			 * changing the allocation type.
			 */
			type = ofw_isa_range_map(sc->sc_range, sc->sc_nrange,
			    &start, &end, &ridx);
			eri = &sc->sc_rinfo[ridx];
			res = rman_reserve_resource(&eri->eri_rman, start,
			    end, count, flags & ~RF_ACTIVE, child);
			if (res == NULL)
				return (NULL);
			rman_set_rid(res, *rid);
			if ((flags & RF_ACTIVE) != 0 && bus_activate_resource(
			    child, type, *rid, res) != 0) {
				rman_release_resource(res);
				return (NULL);
			}
		} else {
			/* Map EBus ranges to nexus ranges. */
			for (i = 0; i < sc->sc_nrange; i++) {
				enr = &((struct ebus_nexus_ranges *)
				    sc->sc_range)[i];
				cstart = (((uint64_t)enr->child_hi) << 32) |
				    enr->child_lo;
				cend = cstart + enr->size - 1;
				if (start >= cstart && end <= cend) {
					offset =
					    (((uint64_t)enr->phys_hi) << 32) |
					    enr->phys_lo;
					start += offset - cstart;
					end += offset - cstart;
					res = bus_generic_alloc_resource(bus,
					    child, type, rid, start, end,
					    count, flags);
					break;
				}
			}
		}
		if (!passthrough)
			rle->res = res;
		return (res);
	case SYS_RES_IRQ:
		return (resource_list_alloc(rl, bus, child, type, rid, start,
		    end, count, flags));
	}
	return (NULL);
}

static int
ebus_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{
	struct ebus_softc *sc;
	struct ebus_rinfo *eri;
	bus_space_tag_t bt;
	bus_space_handle_t bh;
	int i, rv;

	sc = device_get_softc(bus);
	if ((sc->sc_flags & EBUS_PCI) != 0 && type != SYS_RES_IRQ) {
		for (i = 0; i < sc->sc_nrange; i++) {
			eri = &sc->sc_rinfo[i];
			if (rman_is_region_manager(res, &eri->eri_rman) != 0) {
				bt = rman_get_bustag(eri->eri_res);
				rv = bus_space_subregion(bt,
				    rman_get_bushandle(eri->eri_res),
				    rman_get_start(res) -
				    rman_get_start(eri->eri_res),
				    rman_get_size(res), &bh);
				if (rv != 0)
					return (rv);
				rman_set_bustag(res, bt);
				rman_set_bushandle(res, bh);
				return (rman_activate_resource(res));
			}
		}
		return (EINVAL);
	}
	return (bus_generic_activate_resource(bus, child, type, rid, res));
}

static int
ebus_adjust_resource(device_t bus __unused, device_t child __unused,
    int type __unused, struct resource *res __unused, rman_res_t start __unused,
    rman_res_t end __unused)
{

	return (ENXIO);
}

static int
ebus_release_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{
	struct ebus_softc *sc;
	struct resource_list *rl;
	struct resource_list_entry *rle;
	int passthrough, rv;

	passthrough = (device_get_parent(child) != bus);
	rl = BUS_GET_RESOURCE_LIST(bus, child);
	sc = device_get_softc(bus);
	if ((sc->sc_flags & EBUS_PCI) != 0 && type != SYS_RES_IRQ) {
		if ((rman_get_flags(res) & RF_ACTIVE) != 0 ){
			rv = bus_deactivate_resource(child, type, rid, res);
			if (rv != 0)
				return (rv);
		}
		rv = rman_release_resource(res);
		if (rv != 0)
			return (rv);
		if (!passthrough) {
			rle = resource_list_find(rl, type, rid);
			KASSERT(rle != NULL,
			    ("%s: resource entry not found!", __func__));
			KASSERT(rle->res != NULL,
			   ("%s: resource entry is not busy", __func__));
			rle->res = NULL;
		}
		return (0);
	}
	return (resource_list_release(rl, bus, child, type, rid, res));
}

static int
ebus_setup_intr(device_t dev, device_t child, struct resource *ires,
    int flags, driver_filter_t *filt, driver_intr_t *intr, void *arg,
    void **cookiep)
{
#ifndef SUN4V
	struct ebus_softc *sc;
	u_long vec;

	sc = device_get_softc(dev);
	if ((sc->sc_flags & EBUS_PCI) == 0) {
		/*
		 * Make sure the vector is fully specified.  This isn't
		 * necessarily the case with the PCI variant.
		 */
		vec = rman_get_start(ires);
		if (INTIGN(vec) != sc->sc_ign) {
			device_printf(dev,
			    "invalid interrupt vector 0x%lx\n", vec);
			return (EINVAL);
		}

		/*
		 * As we rely on the interrupt controllers of the
		 * accompanying PCI-Express bridge ensure at least
		 * something is registered for this vector.
		 */
		if (intr_vectors[vec].iv_ic == NULL) {
			device_printf(dev,
			    "invalid interrupt controller for vector 0x%lx\n",
			    vec);
			return (EINVAL);
		}
	}
#endif
	return (bus_generic_setup_intr(dev, child, ires, flags, filt, intr,
	    arg, cookiep));
}

static struct resource_list *
ebus_get_resource_list(device_t dev, device_t child)
{
	struct ebus_devinfo *edi;

	edi = device_get_ivars(child);
	return (&edi->edi_rl);
}

static const struct ofw_bus_devinfo *
ebus_get_devinfo(device_t bus, device_t dev)
{
	struct ebus_devinfo *edi;

	edi = device_get_ivars(dev);
	return (&edi->edi_obdinfo);
}

static struct ebus_devinfo *
ebus_setup_dinfo(device_t dev, struct ebus_softc *sc, phandle_t node)
{
	struct isa_regs reg, *regs;
	ofw_isa_intr_t intr, *intrs;
	struct ebus_devinfo *edi;
	uint64_t start;
	uint32_t rintr;
	int i, nintr, nreg, rv;

	edi = malloc(sizeof(*edi), M_DEVBUF, M_ZERO | M_WAITOK);
	if (ofw_bus_gen_setup_devinfo(&edi->edi_obdinfo, node) != 0) {
		free(edi, M_DEVBUF);
		return (NULL);
	}
	resource_list_init(&edi->edi_rl);
	nreg = OF_getprop_alloc_multi(node, "reg", sizeof(*regs), (void **)&regs);
	if (nreg == -1) {
		device_printf(dev, "<%s>: incomplete\n",
		    edi->edi_obdinfo.obd_name);
		ebus_destroy_dinfo(edi);
		return (NULL);
	}
	for (i = 0; i < nreg; i++) {
		start = ISA_REG_PHYS(regs + i);
		(void)resource_list_add(&edi->edi_rl, SYS_RES_MEMORY, i,
		    start, start + regs[i].size - 1, regs[i].size);
	}
	OF_prop_free(regs);

	nintr = OF_getprop_alloc_multi(node, "interrupts",  sizeof(*intrs),
	    (void **)&intrs);
	if (nintr == -1)
		return (edi);
	for (i = 0; i < nintr; i++) {
		rv = 0;
		if ((sc->sc_flags & EBUS_PCI) != 0) {
			rintr = ofw_isa_route_intr(dev, node, &sc->sc_iinfo,
			    intrs[i]);
		} else {
			intr = intrs[i];
			rv = ofw_bus_lookup_imap(node, &sc->sc_iinfo, &reg,
			    sizeof(reg), &intr, sizeof(intr), &rintr,
			    sizeof(rintr), NULL);
#ifndef SUN4V
			if (rv != 0)
				rintr = INTMAP_VEC(sc->sc_ign, rintr);
#endif
		}
		if ((sc->sc_flags & EBUS_PCI) == 0 ? rv == 0 :
		    rintr == PCI_INVALID_IRQ) {
			device_printf(dev,
			    "<%s>: could not map EBus interrupt %d\n",
			    edi->edi_obdinfo.obd_name, intrs[i]);
			continue;
		}
		(void)resource_list_add(&edi->edi_rl, SYS_RES_IRQ, i, rintr,
		    rintr, 1);
	}
	OF_prop_free(intrs);
	return (edi);
}

static void
ebus_destroy_dinfo(struct ebus_devinfo *edi)
{

	resource_list_free(&edi->edi_rl);
	ofw_bus_gen_destroy_devinfo(&edi->edi_obdinfo);
	free(edi, M_DEVBUF);
}

static int
ebus_print_res(struct ebus_devinfo *edi)
{
	int retval;

	retval = 0;
	retval += resource_list_print_type(&edi->edi_rl, "addr", SYS_RES_MEMORY,
	    "%#jx");
	retval += resource_list_print_type(&edi->edi_rl, "irq", SYS_RES_IRQ,
	    "%jd");
	return (retval);
}
