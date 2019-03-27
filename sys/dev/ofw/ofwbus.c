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
#ifdef INTRNG
#include <sys/intr.h>
#endif

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>
#include <dev/fdt/simplebus.h>

#include <machine/bus.h>
#include <machine/resource.h>

/*
 * The ofwbus (which is a pseudo-bus actually) iterates over the nodes that
 * hang from the Open Firmware root node and adds them as devices to this bus
 * (except some special nodes which are excluded) so that drivers can be
 * attached to them.
 *
 */

struct ofwbus_softc {
	struct simplebus_softc simplebus_sc;
	struct rman	sc_intr_rman;
	struct rman	sc_mem_rman;
};

#ifndef __aarch64__
static device_identify_t ofwbus_identify;
#endif
static device_probe_t ofwbus_probe;
static device_attach_t ofwbus_attach;
static bus_alloc_resource_t ofwbus_alloc_resource;
static bus_adjust_resource_t ofwbus_adjust_resource;
static bus_release_resource_t ofwbus_release_resource;

static device_method_t ofwbus_methods[] = {
	/* Device interface */
#ifndef __aarch64__
	DEVMETHOD(device_identify,	ofwbus_identify),
#endif
	DEVMETHOD(device_probe,		ofwbus_probe),
	DEVMETHOD(device_attach,	ofwbus_attach),

	/* Bus interface */
	DEVMETHOD(bus_alloc_resource,	ofwbus_alloc_resource),
	DEVMETHOD(bus_adjust_resource,	ofwbus_adjust_resource),
	DEVMETHOD(bus_release_resource,	ofwbus_release_resource),

	DEVMETHOD_END
};

DEFINE_CLASS_1(ofwbus, ofwbus_driver, ofwbus_methods,
    sizeof(struct ofwbus_softc), simplebus_driver);
static devclass_t ofwbus_devclass;
EARLY_DRIVER_MODULE(ofwbus, nexus, ofwbus_driver, ofwbus_devclass, 0, 0,
    BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
MODULE_VERSION(ofwbus, 1);

#ifndef __aarch64__
static void
ofwbus_identify(driver_t *driver, device_t parent)
{

	/* Check if Open Firmware has been instantiated */
	if (OF_peer(0) == 0)
		return;

	if (device_find_child(parent, "ofwbus", -1) == NULL)
		BUS_ADD_CHILD(parent, 0, "ofwbus", -1);
}
#endif

static int
ofwbus_probe(device_t dev)
{

#ifdef __aarch64__
	if (OF_peer(0) == 0)
		return (ENXIO);
#endif

	device_set_desc(dev, "Open Firmware Device Tree");
	return (BUS_PROBE_NOWILDCARD);
}

static int
ofwbus_attach(device_t dev)
{
	struct ofwbus_softc *sc;
	phandle_t node;
	struct ofw_bus_devinfo obd;

	sc = device_get_softc(dev);

	node = OF_peer(0);

	/*
	 * If no Open Firmware, bail early
	 */
	if (node == -1)
		return (ENXIO);

	/*
	 * ofwbus bus starts on unamed node in FDT, so we cannot make
	 * ofw_bus_devinfo from it. Pass node to simplebus_init directly.
	 */
	simplebus_init(dev, node);
	sc->sc_intr_rman.rm_type = RMAN_ARRAY;
	sc->sc_intr_rman.rm_descr = "Interrupts";
	sc->sc_mem_rman.rm_type = RMAN_ARRAY;
	sc->sc_mem_rman.rm_descr = "Device Memory";
	if (rman_init(&sc->sc_intr_rman) != 0 ||
	    rman_init(&sc->sc_mem_rman) != 0 ||
	    rman_manage_region(&sc->sc_intr_rman, 0, ~0) != 0 ||
	    rman_manage_region(&sc->sc_mem_rman, 0, BUS_SPACE_MAXADDR) != 0)
		panic("%s: failed to set up rmans.", __func__);

	/*
	 * Allow devices to identify.
	 */
	bus_generic_probe(dev);

	/*
	 * Now walk the OFW tree and attach top-level devices.
	 */
	for (node = OF_child(node); node > 0; node = OF_peer(node)) {
		if (ofw_bus_gen_setup_devinfo(&obd, node) != 0)
			continue;
		simplebus_add_device(dev, node, 0, NULL, -1, NULL);
	}
	return (bus_generic_attach(dev));
}

static struct resource *
ofwbus_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct ofwbus_softc *sc;
	struct rman *rm;
	struct resource *rv;
	struct resource_list_entry *rle;
	int isdefault, passthrough;

	isdefault = RMAN_IS_DEFAULT_RANGE(start, end);
	passthrough = (device_get_parent(child) != bus);
	sc = device_get_softc(bus);
	rle = NULL;
	if (!passthrough && isdefault) {
		rle = resource_list_find(BUS_GET_RESOURCE_LIST(bus, child),
		    type, *rid);
		if (rle == NULL) {
			if (bootverbose)
				device_printf(bus, "no default resources for "
				    "rid = %d, type = %d\n", *rid, type);
			return (NULL);
		}
		start = rle->start;
		count = ummax(count, rle->count);
		end = ummax(rle->end, start + count - 1);
	}

	switch (type) {
	case SYS_RES_IRQ:
		rm = &sc->sc_intr_rman;
		break;
	case SYS_RES_MEMORY:
		rm = &sc->sc_mem_rman;
		break;
	default:
		return (NULL);
	}

	rv = rman_reserve_resource(rm, start, end, count, flags & ~RF_ACTIVE,
	    child);
	if (rv == NULL)
		return (NULL);
	rman_set_rid(rv, *rid);

	if ((flags & RF_ACTIVE) != 0 && bus_activate_resource(child, type,
	    *rid, rv) != 0) {
		rman_release_resource(rv);
		return (NULL);
	}

	if (!passthrough && rle != NULL) {
		rle->res = rv;
		rle->start = rman_get_start(rv);
		rle->end = rman_get_end(rv);
		rle->count = rle->end - rle->start + 1;
	}

	return (rv);
}

static int
ofwbus_adjust_resource(device_t bus, device_t child __unused, int type,
    struct resource *r, rman_res_t start, rman_res_t end)
{
	struct ofwbus_softc *sc;
	struct rman *rm;
	device_t ofwbus;

	ofwbus = bus;
	while (strcmp(device_get_name(device_get_parent(ofwbus)), "root") != 0)
		ofwbus = device_get_parent(ofwbus);
	sc = device_get_softc(ofwbus);
	switch (type) {
	case SYS_RES_IRQ:
		rm = &sc->sc_intr_rman;
		break;
	case SYS_RES_MEMORY:
		rm = &sc->sc_mem_rman;
		break;
	default:
		return (EINVAL);
	}
	if (rm == NULL)
		return (ENXIO);
	if (rman_is_region_manager(r, rm) == 0)
		return (EINVAL);
	return (rman_adjust_resource(r, start, end));
}

static int
ofwbus_release_resource(device_t bus, device_t child, int type,
    int rid, struct resource *r)
{
	struct resource_list_entry *rle;
	int passthrough;
	int error;

	passthrough = (device_get_parent(child) != bus);
	if (!passthrough) {
		/* Clean resource list entry */
		rle = resource_list_find(BUS_GET_RESOURCE_LIST(bus, child),
		    type, rid);
		if (rle != NULL)
			rle->res = NULL;
	}

	if ((rman_get_flags(r) & RF_ACTIVE) != 0) {
		error = bus_deactivate_resource(child, type, rid, r);
		if (error)
			return (error);
	}
	return (rman_release_resource(r));
}
