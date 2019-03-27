/*-
 * Copyright 1998 Massachusetts Institute of Technology
 * Copyright 2001 by Thomas Moestl <tmm@FreeBSD.org>.
 * Copyright 2006 by Marius Strobl <marius@FreeBSD.org>.
 * All rights reserved.
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
 * 	from: FreeBSD: src/sys/i386/i386/nexus.c,v 1.43 2001/02/09
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/pcpu.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/resource.h>

/*
 * The nexus handles root-level resource allocation requests and interrupt
 * mapping. All direct subdevices of nexus are attached by DEVICE_IDENTIFY().
 */

static device_probe_t nexus_probe;
static device_attach_t nexus_attach;
static bus_setup_intr_t nexus_setup_intr;
static bus_teardown_intr_t nexus_teardown_intr;
static bus_activate_resource_t nexus_activate_resource;
static bus_deactivate_resource_t nexus_deactivate_resource;
static bus_space_tag_t nexus_get_bus_tag(device_t, device_t);
#ifdef SMP
static bus_bind_intr_t nexus_bind_intr;
#endif
static bus_config_intr_t nexus_config_intr;
static ofw_bus_map_intr_t nexus_ofw_map_intr;

static device_method_t nexus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nexus_probe),
	DEVMETHOD(device_attach,	nexus_attach),

	/* Bus interface */
	DEVMETHOD(bus_add_child,	bus_generic_add_child),
	DEVMETHOD(bus_activate_resource,	nexus_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	nexus_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	nexus_setup_intr),
	DEVMETHOD(bus_teardown_intr,	nexus_teardown_intr),
#ifdef SMP
	DEVMETHOD(bus_bind_intr,	nexus_bind_intr),
#endif
	DEVMETHOD(bus_config_intr,	nexus_config_intr),
	DEVMETHOD(bus_get_bus_tag,	nexus_get_bus_tag),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_map_intr,	nexus_ofw_map_intr),

	DEVMETHOD_END
};

static devclass_t nexus_devclass;

DEFINE_CLASS_0(nexus, nexus_driver, nexus_methods, 1);
EARLY_DRIVER_MODULE(nexus, root, nexus_driver, nexus_devclass, 0, 0,
    BUS_PASS_BUS);
MODULE_VERSION(nexus, 1);

static int
nexus_probe(device_t dev)
{
        
	device_quiet(dev);	/* suppress attach message for neatness */

	return (BUS_PROBE_DEFAULT);
}

static int
nexus_attach(device_t dev)
{

	bus_generic_probe(dev);
	bus_generic_attach(dev);

	return (0);
}

static int
nexus_setup_intr(device_t bus __unused, device_t child, struct resource *r,
    int flags, driver_filter_t *filt, driver_intr_t *intr, void *arg,
    void **cookiep)
{
	int error;

	if (r == NULL)
		panic("%s: NULL interrupt resource!", __func__);

	if ((rman_get_flags(r) & RF_SHAREABLE) == 0)
		flags |= INTR_EXCL;

	/* We depend here on rman_activate_resource() being idempotent. */
	error = rman_activate_resource(r);
	if (error)
		return (error);

	error = powerpc_setup_intr(device_get_nameunit(child),
	    rman_get_start(r), filt, intr, arg, flags, cookiep);

	return (error);
}

static int
nexus_teardown_intr(device_t bus __unused, device_t child __unused,
    struct resource *r, void *ih)
{
        
	if (r == NULL)
		return (EINVAL);

	return (powerpc_teardown_intr(ih));
}

static bus_space_tag_t
nexus_get_bus_tag(device_t bus __unused, device_t child __unused)
{

	return(&bs_be_tag);
}

#ifdef SMP
static int
nexus_bind_intr(device_t bus __unused, device_t child __unused,
    struct resource *r, int cpu)
{

	return (powerpc_bind_intr(rman_get_start(r), cpu));
}
#endif

static int
nexus_config_intr(device_t dev, int irq, enum intr_trigger trig,
    enum intr_polarity pol)
{
 
	return (powerpc_config_intr(irq, trig, pol));
} 

static int
nexus_ofw_map_intr(device_t dev, device_t child, phandle_t iparent, int icells,
    pcell_t *irq)
{
	u_int intr = MAP_IRQ(iparent, irq[0]);
	if (icells > 1)
		powerpc_fw_config_intr(intr, irq[1]);
	return (intr);
}

static int
nexus_activate_resource(device_t bus __unused, device_t child __unused,
    int type, int rid __unused, struct resource *r)
{

	if (type == SYS_RES_MEMORY) {
		vm_paddr_t start;
		void *p;

		start = (vm_paddr_t) rman_get_start(r);
		if (bootverbose)
			printf("nexus mapdev: start %jx, len %jd\n",
			    (uintmax_t)start, rman_get_size(r));

		p = pmap_mapdev(start, (vm_size_t) rman_get_size(r));
		if (p == NULL)
			return (ENOMEM);
		rman_set_virtual(r, p);
		rman_set_bustag(r, &bs_be_tag);
		rman_set_bushandle(r, (u_long)p);
	}
	return (rman_activate_resource(r));
}

static int
nexus_deactivate_resource(device_t bus __unused, device_t child __unused,
    int type __unused, int rid __unused, struct resource *r)
{

	/*
	 * If this is a memory resource, unmap it.
	 */
	if ((type == SYS_RES_MEMORY) || (type == SYS_RES_IOPORT)) {
		bus_size_t psize;

		psize = rman_get_size(r);
		pmap_unmapdev((vm_offset_t)rman_get_virtual(r), psize);
	}

	return (rman_deactivate_resource(r));
}

