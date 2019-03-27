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

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/bus_common.h>
#include <machine/intr_machdep.h>
#include <machine/nexusvar.h>
#include <machine/ofw_nexus.h>
#include <machine/resource.h>
#include <machine/ver.h>

#include <sys/rman.h>

/*
 * The nexus (which is a pseudo-bus actually) iterates over the nodes that
 * hang from the Open Firmware root node and adds them as devices to this bus
 * (except some special nodes which are excluded) so that drivers can be
 * attached to them.
 *
 * Additionally, interrupt setup/teardown and some resource management are
 * done at this level.
 *
 * Maybe this code should get into dev/ofw to some extent, as some of it should
 * work for all Open Firmware based machines...
 */

struct nexus_devinfo {
	struct ofw_bus_devinfo	ndi_obdinfo;
	struct resource_list	ndi_rl;
};

struct nexus_softc {
	struct rman	sc_intr_rman;
	struct rman	sc_mem_rman;
};

static device_probe_t nexus_probe;
static device_attach_t nexus_attach;
static bus_print_child_t nexus_print_child;
static bus_add_child_t nexus_add_child;
static bus_probe_nomatch_t nexus_probe_nomatch;
static bus_setup_intr_t nexus_setup_intr;
static bus_teardown_intr_t nexus_teardown_intr;
static bus_alloc_resource_t nexus_alloc_resource;
static bus_activate_resource_t nexus_activate_resource;
static bus_deactivate_resource_t nexus_deactivate_resource;
static bus_adjust_resource_t nexus_adjust_resource;
static bus_release_resource_t nexus_release_resource;
static bus_get_resource_list_t nexus_get_resource_list;
#ifdef SMP
static bus_bind_intr_t nexus_bind_intr;
#endif
static bus_describe_intr_t nexus_describe_intr;
static bus_get_dma_tag_t nexus_get_dma_tag;
static bus_get_bus_tag_t nexus_get_bus_tag;
static ofw_bus_get_devinfo_t nexus_get_devinfo;

static int nexus_inlist(const char *, const char *const *);
static struct nexus_devinfo * nexus_setup_dinfo(device_t, phandle_t);
static void nexus_destroy_dinfo(struct nexus_devinfo *);
static int nexus_print_res(struct nexus_devinfo *);

static device_method_t nexus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nexus_probe),
	DEVMETHOD(device_attach,	nexus_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	nexus_print_child),
	DEVMETHOD(bus_probe_nomatch,	nexus_probe_nomatch),
	DEVMETHOD(bus_read_ivar,	bus_generic_read_ivar),
	DEVMETHOD(bus_write_ivar,	bus_generic_write_ivar),
	DEVMETHOD(bus_add_child,	nexus_add_child),
	DEVMETHOD(bus_alloc_resource,	nexus_alloc_resource),
	DEVMETHOD(bus_activate_resource,	nexus_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	nexus_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,	nexus_adjust_resource),
	DEVMETHOD(bus_release_resource,	nexus_release_resource),
	DEVMETHOD(bus_setup_intr,	nexus_setup_intr),
	DEVMETHOD(bus_teardown_intr,	nexus_teardown_intr),
	DEVMETHOD(bus_set_resource,	bus_generic_rl_set_resource),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_get_resource_list, nexus_get_resource_list),
#ifdef SMP
	DEVMETHOD(bus_bind_intr,	nexus_bind_intr),
#endif
	DEVMETHOD(bus_describe_intr,	nexus_describe_intr),
	DEVMETHOD(bus_get_dma_tag,	nexus_get_dma_tag),
	DEVMETHOD(bus_get_bus_tag,	nexus_get_bus_tag),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	nexus_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static devclass_t nexus_devclass;

DEFINE_CLASS_0(nexus, nexus_driver, nexus_methods, sizeof(struct nexus_softc));
EARLY_DRIVER_MODULE(nexus, root, nexus_driver, nexus_devclass, 0, 0,
    BUS_PASS_BUS);
MODULE_VERSION(nexus, 1);

static const char *const nexus_excl_name[] = {
	"FJSV,system",
	"aliases",
	"associations",
	"chosen",
	"cmp",
	"counter-timer",	/* No separate device; handled by psycho/sbus */
	"failsafe",
	"memory",
	"openprom",
	"options",
	"packages",
	"physical-memory",
	"rsc",
	"sgcn",
	"todsg",
	"virtual-memory",
	NULL
};

static const char *const nexus_excl_type[] = {
	"core",
	"cpu",
	NULL
};

extern struct bus_space_tag nexus_bustag;
extern struct bus_dma_tag nexus_dmatag;

static int
nexus_inlist(const char *name, const char *const *list)
{
	int i;

	if (name == NULL)
		return (0);
	for (i = 0; list[i] != NULL; i++)
		if (strcmp(name, list[i]) == 0)
			return (1);
	return (0);
}

#define	NEXUS_EXCLUDED(name, type)					\
	(nexus_inlist((name), nexus_excl_name) ||			\
	((type) != NULL && nexus_inlist((type), nexus_excl_type)))

static int
nexus_probe(device_t dev)
{

	/* Nexus does always match. */
	device_set_desc(dev, "Open Firmware Nexus device");
	return (0);
}

static int
nexus_attach(device_t dev)
{
	struct nexus_devinfo *ndi;
	struct nexus_softc *sc;
	device_t cdev;
	phandle_t node;

	if (strcmp(device_get_name(device_get_parent(dev)), "root") == 0) {
		node = OF_peer(0);
		if (node == -1)
			panic("%s: OF_peer failed.", __func__);

		sc = device_get_softc(dev);
		sc->sc_intr_rman.rm_type = RMAN_ARRAY;
		sc->sc_intr_rman.rm_descr = "Interrupts";
		sc->sc_mem_rman.rm_type = RMAN_ARRAY;
		sc->sc_mem_rman.rm_descr = "Device Memory";
		if (rman_init(&sc->sc_intr_rman) != 0 ||
		    rman_init(&sc->sc_mem_rman) != 0 ||
		    rman_manage_region(&sc->sc_intr_rman, 0,
		    IV_MAX - 1) != 0 ||
		    rman_manage_region(&sc->sc_mem_rman, 0, BUS_SPACE_MAXADDR) != 0)
			panic("%s: failed to set up rmans.", __func__);
	} else
		node = ofw_bus_get_node(dev);

	/*
	 * Allow devices to identify.
	 */
	bus_generic_probe(dev);

	/*
	 * Now walk the OFW tree and attach top-level devices.
	 */
	for (node = OF_child(node); node > 0; node = OF_peer(node)) {
		if ((ndi = nexus_setup_dinfo(dev, node)) == NULL)
			continue;
		cdev = device_add_child(dev, NULL, -1);
		if (cdev == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
			    ndi->ndi_obdinfo.obd_name);
			nexus_destroy_dinfo(ndi);
			continue;
		}
		device_set_ivars(cdev, ndi);
	}
	return (bus_generic_attach(dev));
}

static device_t
nexus_add_child(device_t dev, u_int order, const char *name, int unit)
{
	device_t cdev;
	struct nexus_devinfo *ndi;

	cdev = device_add_child_ordered(dev, order, name, unit);
	if (cdev == NULL)
		return (NULL);

	ndi = malloc(sizeof(*ndi), M_DEVBUF, M_WAITOK | M_ZERO);
	ndi->ndi_obdinfo.obd_node = -1;
	ndi->ndi_obdinfo.obd_name = strdup(name, M_OFWPROP);
	resource_list_init(&ndi->ndi_rl);
	device_set_ivars(cdev, ndi);

	return (cdev);
}

static int
nexus_print_child(device_t bus, device_t child)
{
	int rv;

	rv = bus_print_child_header(bus, child);
	rv += nexus_print_res(device_get_ivars(child));
	rv += bus_print_child_footer(bus, child);
	return (rv);
}

static void
nexus_probe_nomatch(device_t bus, device_t child)
{
	const char *type;

	device_printf(bus, "<%s>", ofw_bus_get_name(child));
	nexus_print_res(device_get_ivars(child));
	type = ofw_bus_get_type(child);
	printf(" type %s (no driver attached)\n",
	    type != NULL ? type : "unknown");
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

	error = inthand_add(device_get_nameunit(child), rman_get_start(r),
	    filt, intr, arg, flags, cookiep);

	/*
	 * XXX in case of the AFB/FFB interrupt and a Psycho, Sabre or U2S
	 * bridge enable the interrupt in the respective bridge.
	 */

	return (error);
}

static int
nexus_teardown_intr(device_t bus __unused, device_t child __unused,
    struct resource *r, void *ih)
{

	inthand_remove(rman_get_start(r), ih);
	return (0);
}

#ifdef SMP
static int
nexus_bind_intr(device_t bus __unused, device_t child __unused,
    struct resource *r, int cpu)
{

	return (intr_bind(rman_get_start(r), cpu));
}
#endif

static int
nexus_describe_intr(device_t bus __unused, device_t child __unused,
    struct resource *r, void *cookie, const char *descr)
{

	return (intr_describe(rman_get_start(r), cookie, descr));
}

static struct resource *
nexus_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct nexus_softc *sc;
	struct rman *rm;
	struct resource *rv;
	struct resource_list_entry *rle;
	device_t nexus;
	int isdefault, passthrough;

	isdefault = RMAN_IS_DEFAULT_RANGE(start, end);
	passthrough = (device_get_parent(child) != bus);
	nexus = bus;
	while (strcmp(device_get_name(device_get_parent(nexus)), "root") != 0)
		nexus = device_get_parent(nexus);
	sc = device_get_softc(nexus);
	rle = NULL;

	if (!passthrough) {
		rle = resource_list_find(BUS_GET_RESOURCE_LIST(bus, child),
		    type, *rid);
		if (rle == NULL)
			return (NULL);
		if (rle->res != NULL)
			panic("%s: resource entry is busy", __func__);
		if (isdefault) {
			start = rle->start;
			count = ulmax(count, rle->count);
			end = ulmax(rle->end, start + count - 1);
		}
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

	if (!passthrough) {
		rle->res = rv;
		rle->start = rman_get_start(rv);
		rle->end = rman_get_end(rv);
		rle->count = rle->end - rle->start + 1;
	}

	return (rv);
}

static int
nexus_activate_resource(device_t bus __unused, device_t child __unused,
    int type, int rid __unused, struct resource *r)
{

	if (type == SYS_RES_MEMORY) {
		rman_set_bustag(r, &nexus_bustag);
		rman_set_bushandle(r, rman_get_start(r));
	}
	return (rman_activate_resource(r));
}

static int
nexus_deactivate_resource(device_t bus __unused, device_t child __unused,
    int type __unused, int rid __unused, struct resource *r)
{

	return (rman_deactivate_resource(r));
}

static int
nexus_adjust_resource(device_t bus, device_t child __unused, int type,
    struct resource *r, rman_res_t start, rman_res_t end)
{
	struct nexus_softc *sc;
	struct rman *rm;
	device_t nexus;

	nexus = bus;
	while (strcmp(device_get_name(device_get_parent(nexus)), "root") != 0)
		nexus = device_get_parent(nexus);
	sc = device_get_softc(nexus);
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
nexus_release_resource(device_t bus __unused, device_t child, int type,
    int rid, struct resource *r)
{
	int error;

	if ((rman_get_flags(r) & RF_ACTIVE) != 0) {
		error = bus_deactivate_resource(child, type, rid, r);
		if (error)
			return (error);
	}
	return (rman_release_resource(r));
}

static struct resource_list *
nexus_get_resource_list(device_t bus __unused, device_t child)
{
	struct nexus_devinfo *ndi;

	ndi = device_get_ivars(child);
	return (&ndi->ndi_rl);
}

static bus_dma_tag_t
nexus_get_dma_tag(device_t bus __unused, device_t child __unused)
{

	return (&nexus_dmatag);
}

static bus_space_tag_t
nexus_get_bus_tag(device_t bus __unused, device_t child __unused)
{

	return (&nexus_bustag);
}

static const struct ofw_bus_devinfo *
nexus_get_devinfo(device_t bus __unused, device_t child)
{
	struct nexus_devinfo *ndi;

	ndi = device_get_ivars(child);
	return (&ndi->ndi_obdinfo);
}

static struct nexus_devinfo *
nexus_setup_dinfo(device_t dev, phandle_t node)
{
	struct nexus_devinfo *ndi;
	struct nexus_regs *reg;
	bus_addr_t phys;
	bus_size_t size;
	uint32_t ign;
	uint32_t *intr;
	int i;
	int nintr;
	int nreg;

	ndi = malloc(sizeof(*ndi), M_DEVBUF, M_WAITOK | M_ZERO);
	if (ofw_bus_gen_setup_devinfo(&ndi->ndi_obdinfo, node) != 0) {
		free(ndi, M_DEVBUF);
		return (NULL);
	}
	if (NEXUS_EXCLUDED(ndi->ndi_obdinfo.obd_name,
	    ndi->ndi_obdinfo.obd_type)) {
		ofw_bus_gen_destroy_devinfo(&ndi->ndi_obdinfo);
		free(ndi, M_DEVBUF);
		return (NULL);
	}
	resource_list_init(&ndi->ndi_rl);
	nreg = OF_getprop_alloc_multi(node, "reg", sizeof(*reg), (void **)&reg);
	if (nreg == -1) {
		device_printf(dev, "<%s>: incomplete\n",
		    ndi->ndi_obdinfo.obd_name);
		goto fail;
	}
	for (i = 0; i < nreg; i++) {
		phys = NEXUS_REG_PHYS(&reg[i]);
		size = NEXUS_REG_SIZE(&reg[i]);
		/* Skip the dummy reg property of glue devices like ssm(4). */
		if (size != 0)
			resource_list_add(&ndi->ndi_rl, SYS_RES_MEMORY, i,
			    phys, phys + size - 1, size);
	}
	OF_prop_free(reg);

	nintr = OF_getprop_alloc_multi(node, "interrupts",  sizeof(*intr),
	    (void **)&intr);
	if (nintr > 0) {
		if (OF_getprop(node, PCPU_GET(impl) < CPU_IMPL_ULTRASPARCIII ?
		    "upa-portid" : "portid", &ign, sizeof(ign)) <= 0) {
			device_printf(dev, "<%s>: could not determine portid\n",
			    ndi->ndi_obdinfo.obd_name);
			OF_prop_free(intr);
			goto fail;
		}

		/* XXX 7-bit MID on Starfire */
		ign = (ign << INTMAP_IGN_SHIFT) & INTMAP_IGN_MASK;
		for (i = 0; i < nintr; i++) {
			intr[i] |= ign;
			resource_list_add(&ndi->ndi_rl, SYS_RES_IRQ, i, intr[i],
			    intr[i], 1);
		}
		OF_prop_free(intr);
	}

	return (ndi);

 fail:
	nexus_destroy_dinfo(ndi);
	return (NULL);
}

static void
nexus_destroy_dinfo(struct nexus_devinfo *ndi)
{

	resource_list_free(&ndi->ndi_rl);
	ofw_bus_gen_destroy_devinfo(&ndi->ndi_obdinfo);
	free(ndi, M_DEVBUF);
}

static int
nexus_print_res(struct nexus_devinfo *ndi)
{
	int rv;

	rv = 0;
	rv += resource_list_print_type(&ndi->ndi_rl, "mem", SYS_RES_MEMORY,
	    "%#jx");
	rv += resource_list_print_type(&ndi->ndi_rl, "irq", SYS_RES_IRQ,
	    "%jd");
	return (rv);
}
