/*
 * Copyright (c) 2014 Roger Pau Monné <roger.pau@citrix.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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
#include <sys/pcpu.h>
#include <sys/rman.h>
#include <sys/smp.h>
#include <sys/limits.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_param.h>
#include <vm/vm_phys.h>

#include <xen/xen-os.h>
#include <xen/gnttab.h>

#include "xenmem_if.h"

/*
 * Allocate unused physical memory above 4GB in order to map memory
 * from foreign domains. We use memory starting at 4GB in order to
 * prevent clashes with MMIO/ACPI regions.
 *
 * Since this is not possible on i386 just use any available memory
 * chunk and hope we don't clash with anything else.
 */
#ifdef __amd64__
#define LOW_MEM_LIMIT	0x100000000ul
#else
#define LOW_MEM_LIMIT	0
#endif

static devclass_t xenpv_devclass;

static void
xenpv_identify(driver_t *driver, device_t parent)
{
	if (!xen_domain())
		return;

	/* Make sure there's only one xenpv device. */
	if (devclass_get_device(xenpv_devclass, 0))
		return;

	/*
	 * The xenpv bus should be the last to attach in order
	 * to properly detect if an ISA bus has already been added.
	 */
	if (BUS_ADD_CHILD(parent, UINT_MAX, "xenpv", 0) == NULL)
		panic("Unable to attach xenpv bus.");
}

static int
xenpv_probe(device_t dev)
{

	device_set_desc(dev, "Xen PV bus");
	return (BUS_PROBE_NOWILDCARD);
}

static int
xenpv_attach(device_t dev)
{
	int error;

	/*
	 * Let our child drivers identify any child devices that they
	 * can find.  Once that is done attach any devices that we
	 * found.
	 */
	error = bus_generic_probe(dev);
	if (error)
		return (error);

	error = bus_generic_attach(dev);

	return (error);
}

static struct resource *
xenpv_alloc_physmem(device_t dev, device_t child, int *res_id, size_t size)
{
	struct resource *res;
	vm_paddr_t phys_addr;
	int error;

	res = bus_alloc_resource(child, SYS_RES_MEMORY, res_id, LOW_MEM_LIMIT,
	    ~0, size, RF_ACTIVE);
	if (res == NULL)
		return (NULL);

	phys_addr = rman_get_start(res);
	error = vm_phys_fictitious_reg_range(phys_addr, phys_addr + size,
	    VM_MEMATTR_DEFAULT);
	if (error) {
		bus_release_resource(child, SYS_RES_MEMORY, *res_id, res);
		return (NULL);
	}

	return (res);
}

static int
xenpv_free_physmem(device_t dev, device_t child, int res_id, struct resource *res)
{
	vm_paddr_t phys_addr;
	size_t size;

	phys_addr = rman_get_start(res);
	size = rman_get_size(res);

	vm_phys_fictitious_unreg_range(phys_addr, phys_addr + size);
	return (bus_release_resource(child, SYS_RES_MEMORY, res_id, res));
}

static device_method_t xenpv_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,		xenpv_identify),
	DEVMETHOD(device_probe,			xenpv_probe),
	DEVMETHOD(device_attach,		xenpv_attach),
	DEVMETHOD(device_suspend,		bus_generic_suspend),
	DEVMETHOD(device_resume,		bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_add_child,		bus_generic_add_child),
	DEVMETHOD(bus_alloc_resource,		bus_generic_alloc_resource),
	DEVMETHOD(bus_release_resource,		bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),

	/* Interface to allocate memory for foreign mappings */
	DEVMETHOD(xenmem_alloc,			xenpv_alloc_physmem),
	DEVMETHOD(xenmem_free,			xenpv_free_physmem),

	DEVMETHOD_END
};

static driver_t xenpv_driver = {
	"xenpv",
	xenpv_methods,
	0,
};

DRIVER_MODULE(xenpv, nexus, xenpv_driver, xenpv_devclass, 0, 0);

struct resource *
xenmem_alloc(device_t dev, int *res_id, size_t size)
{
	device_t parent;

	parent = device_get_parent(dev);
	if (parent == NULL)
		return (NULL);
	return (XENMEM_ALLOC(parent, dev, res_id, size));
}

int
xenmem_free(device_t dev, int res_id, struct resource *res)
{
	device_t parent;

	parent = device_get_parent(dev);
	if (parent == NULL)
		return (ENXIO);
	return (XENMEM_FREE(parent, dev, res_id, res));
}
