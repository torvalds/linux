/*-
 * Copyright 1998 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * This code implements a `root nexus' for Arm Architecture
 * machines.  The function of the root nexus is to serve as an
 * attachment point for both processors and buses, and to manage
 * resources which are common to all of them.  In particular,
 * this code implements the core resource managers for interrupt
 * requests, DMA requests (which rightfully should be a part of the
 * ISA code but it's easier to do it here for now), I/O port addresses,
 * and I/O memory address space.
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/interrupt.h>

#include <machine/vmparam.h>
#include <machine/pcb.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/resource.h>
#include <machine/intr.h>

#include <arm/arm/nexusvar.h>

#ifdef FDT
#include <machine/fdt.h>
#include <dev/ofw/ofw_bus_subr.h>
#include "ofw_bus_if.h"
#endif

static MALLOC_DEFINE(M_NEXUSDEV, "nexusdev", "Nexus device");

struct nexus_device {
	struct resource_list	nx_resources;
};

#define DEVTONX(dev)	((struct nexus_device *)device_get_ivars(dev))

static struct rman mem_rman;

static	int nexus_probe(device_t);
static	int nexus_attach(device_t);
static	int nexus_print_child(device_t, device_t);
static	device_t nexus_add_child(device_t, u_int, const char *, int);
static	struct resource *nexus_alloc_resource(device_t, device_t, int, int *,
    rman_res_t, rman_res_t, rman_res_t, u_int);
static	int nexus_activate_resource(device_t, device_t, int, int,
    struct resource *);
static bus_space_tag_t nexus_get_bus_tag(device_t, device_t);
static bus_dma_tag_t nexus_get_dma_tag(device_t dev, device_t child);
#ifdef INTRNG
#ifdef SMP
static	int nexus_bind_intr(device_t, device_t, struct resource *, int);
#endif
#endif
static int nexus_config_intr(device_t dev, int irq, enum intr_trigger trig,
    enum intr_polarity pol);
#ifdef INTRNG
static	int nexus_describe_intr(device_t dev, device_t child,
    struct resource *irq, void *cookie, const char *descr);
#endif
static	int nexus_deactivate_resource(device_t, device_t, int, int,
    struct resource *);
static int nexus_release_resource(device_t, device_t, int, int,
    struct resource *);

static int nexus_setup_intr(device_t dev, device_t child, struct resource *res,
    int flags, driver_filter_t *filt, driver_intr_t *intr, void *arg, void **cookiep);
static int nexus_teardown_intr(device_t, device_t, struct resource *, void *);

#ifdef FDT
static int nexus_ofw_map_intr(device_t dev, device_t child, phandle_t iparent,
    int icells, pcell_t *intr);
#endif

/*
 * Normally NULL (which results in defaults which are handled in
 * busdma_machdep), platform init code can use nexus_set_dma_tag() to set this
 * to a tag that will be inherited by all busses and devices on the platform.
 */
static bus_dma_tag_t nexus_dma_tag;

static device_method_t nexus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nexus_probe),
	DEVMETHOD(device_attach,	nexus_attach),
	/* Bus interface */
	DEVMETHOD(bus_print_child,	nexus_print_child),
	DEVMETHOD(bus_add_child,	nexus_add_child),
	DEVMETHOD(bus_alloc_resource,	nexus_alloc_resource),
	DEVMETHOD(bus_activate_resource,	nexus_activate_resource),
	DEVMETHOD(bus_config_intr,	nexus_config_intr),
	DEVMETHOD(bus_deactivate_resource,	nexus_deactivate_resource),
	DEVMETHOD(bus_release_resource,	nexus_release_resource),
	DEVMETHOD(bus_setup_intr,	nexus_setup_intr),
	DEVMETHOD(bus_teardown_intr,	nexus_teardown_intr),
	DEVMETHOD(bus_get_bus_tag,	nexus_get_bus_tag),
	DEVMETHOD(bus_get_dma_tag,	nexus_get_dma_tag),
#ifdef INTRNG
	DEVMETHOD(bus_describe_intr,	nexus_describe_intr),
#ifdef SMP
	DEVMETHOD(bus_bind_intr,	nexus_bind_intr),
#endif
#endif
#ifdef FDT
	DEVMETHOD(ofw_bus_map_intr,	nexus_ofw_map_intr),
#endif
	{ 0, 0 }
};

static devclass_t nexus_devclass;
static driver_t nexus_driver = {
	"nexus",
	nexus_methods,
	1			/* no softc */
};
EARLY_DRIVER_MODULE(nexus, root, nexus_driver, nexus_devclass, 0, 0,
    BUS_PASS_BUS + BUS_PASS_ORDER_EARLY);

static int
nexus_probe(device_t dev)
{

	device_quiet(dev);	/* suppress attach message for neatness */

	return (BUS_PROBE_DEFAULT);
}

static int
nexus_attach(device_t dev)
{

	mem_rman.rm_start = 0;
	mem_rman.rm_end = BUS_SPACE_MAXADDR;
	mem_rman.rm_type = RMAN_ARRAY;
	mem_rman.rm_descr = "I/O memory addresses";
	if (rman_init(&mem_rman) ||
	    rman_manage_region(&mem_rman, 0, BUS_SPACE_MAXADDR))
		panic("nexus_probe mem_rman");

	/*
	 * First, deal with the children we know about already
	 */
	bus_generic_probe(dev);
	bus_generic_attach(dev);

	return (0);
}

static int
nexus_print_child(device_t bus, device_t child)
{
	int retval = 0;

	retval += bus_print_child_header(bus, child);
	retval += printf("\n");

	return (retval);
}

static device_t
nexus_add_child(device_t bus, u_int order, const char *name, int unit)
{
	device_t child;
	struct nexus_device *ndev;

	ndev = malloc(sizeof(struct nexus_device), M_NEXUSDEV, M_NOWAIT|M_ZERO);
	if (!ndev)
		return (0);
	resource_list_init(&ndev->nx_resources);

	child = device_add_child_ordered(bus, order, name, unit);

	/* should we free this in nexus_child_detached? */
	device_set_ivars(child, ndev);

	return (child);
}


/*
 * Allocate a resource on behalf of child.  NB: child is usually going to be a
 * child of one of our descendants, not a direct child of nexus0.
 * (Exceptions include footbridge.)
 */
static struct resource *
nexus_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource *rv;
	struct rman *rm;
	int needactivate = flags & RF_ACTIVE;

	flags &= ~RF_ACTIVE;

	switch (type) {
	case SYS_RES_MEMORY:
	case SYS_RES_IOPORT:
		rm = &mem_rman;
		break;

	default:
		return (NULL);
	}

	rv = rman_reserve_resource(rm, start, end, count, flags, child);
	if (rv == NULL)
		return (NULL);

	rman_set_rid(rv, *rid);

	if (needactivate) {
		if (bus_activate_resource(child, type, *rid, rv)) {
			rman_release_resource(rv);
			return (0);
		}
	}

	return (rv);
}

static int
nexus_release_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{
	int error;

	if (rman_get_flags(res) & RF_ACTIVE) {
		error = bus_deactivate_resource(child, type, rid, res);
		if (error)
			return (error);
	}
	return (rman_release_resource(res));
}

static bus_space_tag_t
nexus_get_bus_tag(device_t bus __unused, device_t child __unused)
{

#ifdef FDT
		return(fdtbus_bs_tag);
#else
		return((void *)1);
#endif
}

static bus_dma_tag_t
nexus_get_dma_tag(device_t dev, device_t child)
{

	return nexus_dma_tag;
}

void
nexus_set_dma_tag(bus_dma_tag_t tag)
{

	nexus_dma_tag = tag;
}

static int
nexus_config_intr(device_t dev, int irq, enum intr_trigger trig,
    enum intr_polarity pol)
{
	int ret = ENODEV;

#ifdef INTRNG
	device_printf(dev, "bus_config_intr is obsolete and not supported!\n");
	ret = EOPNOTSUPP;
#else
	if (arm_config_irq)
		ret = (*arm_config_irq)(irq, trig, pol);
#endif
	return (ret);
}

static int
nexus_setup_intr(device_t dev, device_t child, struct resource *res, int flags,
    driver_filter_t *filt, driver_intr_t *intr, void *arg, void **cookiep)
{
#ifndef INTRNG
	int irq;
#endif

	if ((rman_get_flags(res) & RF_SHAREABLE) == 0)
		flags |= INTR_EXCL;

#ifdef INTRNG
	return(intr_setup_irq(child, res, filt, intr, arg, flags, cookiep));
#else
	for (irq = rman_get_start(res); irq <= rman_get_end(res); irq++) {
		arm_setup_irqhandler(device_get_nameunit(child),
		    filt, intr, arg, irq, flags, cookiep);
		arm_unmask_irq(irq);
	}
	return (0);
#endif
}

static int
nexus_teardown_intr(device_t dev, device_t child, struct resource *r, void *ih)
{

#ifdef INTRNG
	return (intr_teardown_irq(child, r, ih));
#else
	return (arm_remove_irqhandler(rman_get_start(r), ih));
#endif
}

#ifdef INTRNG
static int
nexus_describe_intr(device_t dev, device_t child, struct resource *irq,
    void *cookie, const char *descr)
{

	return (intr_describe_irq(child, irq, cookie, descr));
}

#ifdef SMP
static int
nexus_bind_intr(device_t dev, device_t child, struct resource *irq, int cpu)
{

	return (intr_bind_irq(child, irq, cpu));
}
#endif
#endif

static int
nexus_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	int err;
	bus_addr_t paddr;
	bus_size_t psize;
	bus_space_handle_t vaddr;

	if ((err = rman_activate_resource(r)) != 0)
		return (err);

	/*
	 * If this is a memory resource, map it into the kernel.
	 */
	if (type == SYS_RES_MEMORY || type == SYS_RES_IOPORT) {
		paddr = (bus_addr_t)rman_get_start(r);
		psize = (bus_size_t)rman_get_size(r);
#ifdef FDT
		err = bus_space_map(fdtbus_bs_tag, paddr, psize, 0, &vaddr);
		if (err != 0) {
			rman_deactivate_resource(r);
			return (err);
		}
		rman_set_bustag(r, fdtbus_bs_tag);
#else
		vaddr = (bus_space_handle_t)pmap_mapdev((vm_offset_t)paddr,
		    (vm_size_t)psize);
		if (vaddr == 0) {
			rman_deactivate_resource(r);
			return (ENOMEM);
		}
		rman_set_bustag(r, (void *)1);
#endif
		rman_set_virtual(r, (void *)vaddr);
		rman_set_bushandle(r, vaddr);
		return (0);
	} else if (type == SYS_RES_IRQ) {
#ifdef INTRNG
		err = intr_activate_irq(child, r);
		if (err != 0) {
			rman_deactivate_resource(r);
			return (err);
		}
#endif
	}
	return (0);
}

static int
nexus_deactivate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	bus_size_t psize;
	bus_space_handle_t vaddr;

	if (type == SYS_RES_MEMORY || type == SYS_RES_IOPORT) {
		psize = (bus_size_t)rman_get_size(r);
		vaddr = rman_get_bushandle(r);

		if (vaddr != 0) {
#ifdef FDT
			bus_space_unmap(fdtbus_bs_tag, vaddr, psize);
#else
			pmap_unmapdev((vm_offset_t)vaddr, (vm_size_t)psize);
#endif
			rman_set_virtual(r, NULL);
			rman_set_bushandle(r, 0);
		}
	} else if (type == SYS_RES_IRQ) {
#ifdef INTRNG
		intr_deactivate_irq(child, r);
#endif
	}

	return (rman_deactivate_resource(r));
}

#ifdef FDT
static int
nexus_ofw_map_intr(device_t dev, device_t child, phandle_t iparent, int icells,
    pcell_t *intr)
{
#ifndef INTRNG
	return (intr_fdt_map_irq(iparent, intr, icells));
#else
	u_int irq;
	struct intr_map_data_fdt *fdt_data;
	size_t len;

	len = sizeof(*fdt_data) + icells * sizeof(pcell_t);
	fdt_data = (struct intr_map_data_fdt *)intr_alloc_map_data(
	    INTR_MAP_DATA_FDT, len, M_WAITOK | M_ZERO);
	fdt_data->iparent = iparent;
	fdt_data->ncells = icells;
	memcpy(fdt_data->cells, intr, icells * sizeof(pcell_t));
	irq = intr_map_irq(NULL, iparent, (struct intr_map_data *)fdt_data);
	return (irq);
#endif /* INTRNG */
}
#endif /* FDT */

