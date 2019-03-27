/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Olivier Houchard.  All rights reserved.
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
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/types.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>

#include <dev/ic/i8259.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>

#include <mips/malta/gtvar.h>

static int
gt_probe(device_t dev)
{
	device_set_desc(dev, "GT64120 chip");
	return (BUS_PROBE_NOWILDCARD);
}

static void
gt_identify(driver_t *drv, device_t parent)
{
	BUS_ADD_CHILD(parent, 0, "gt", 0);
}

static int
gt_attach(device_t dev)
{
	struct gt_softc *sc = device_get_softc(dev);
	sc->dev = dev;

	device_add_child(dev, "pcib", 0);
	bus_generic_probe(dev);
	bus_generic_attach(dev);


	return (0);
}

static struct resource *
gt_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	return (BUS_ALLOC_RESOURCE(device_get_parent(dev), child,
		    type, rid, start, end, count, flags));
	
}

static int
gt_setup_intr(device_t dev, device_t child,
    struct resource *ires, int flags, driver_filter_t *filt, 
    driver_intr_t *intr, void *arg, void **cookiep)
{
	return BUS_SETUP_INTR(device_get_parent(dev), child, ires, flags, 
	    filt, intr, arg, cookiep);
}

static int
gt_teardown_intr(device_t dev, device_t child, struct resource *res,
    void *cookie)
{
	return (BUS_TEARDOWN_INTR(device_get_parent(dev), child, res, cookie));
}

static int
gt_activate_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{
	return (BUS_ACTIVATE_RESOURCE(device_get_parent(dev), child,
		    type, rid, r));
}

static device_method_t gt_methods[] = {
	DEVMETHOD(device_probe, gt_probe),
	DEVMETHOD(device_identify, gt_identify),
	DEVMETHOD(device_attach, gt_attach),

	DEVMETHOD(bus_setup_intr, gt_setup_intr),
	DEVMETHOD(bus_teardown_intr, gt_teardown_intr),
	DEVMETHOD(bus_alloc_resource, gt_alloc_resource),
	DEVMETHOD(bus_activate_resource, gt_activate_resource),

	DEVMETHOD_END
};

static driver_t gt_driver = {
	"gt",
	gt_methods,
	sizeof(struct gt_softc),
};
static devclass_t gt_devclass;

DRIVER_MODULE(gt, nexus, gt_driver, gt_devclass, 0, 0);
