/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999-2002 Eduardo Horvath
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
 *	from: NetBSD: sbus.c,v 1.50 2002/06/20 18:26:24 eeh Exp
 */
/*-
 * Copyright (c) 2002 by Thomas Moestl <tmm@FreeBSD.org>.
 * Copyright (c) 2005 Marius Strobl <marius@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
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

/*
 * SBus support.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/pcpu.h>
#include <sys/queue.h>
#include <sys/reboot.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/bus_common.h>
#include <machine/bus_private.h>
#include <machine/iommureg.h>
#include <machine/iommuvar.h>
#include <machine/resource.h>

#include <sparc64/sbus/ofw_sbus.h>
#include <sparc64/sbus/sbusreg.h>
#include <sparc64/sbus/sbusvar.h>

struct sbus_devinfo {
	int			sdi_burstsz;
	int			sdi_clockfreq;
	int			sdi_slot;

	struct ofw_bus_devinfo	sdi_obdinfo;
	struct resource_list	sdi_rl;
};

/* Range descriptor, allocated for each sc_range. */
struct sbus_rd {
	bus_addr_t		rd_poffset;
	bus_addr_t		rd_pend;
	int			rd_slot;
	bus_addr_t		rd_coffset;
	bus_addr_t		rd_cend;
	struct rman		rd_rman;
	bus_space_handle_t	rd_bushandle;
	struct resource		*rd_res;
};

struct sbus_softc {
	device_t		sc_dev;
	bus_dma_tag_t		sc_cdmatag;
	int			sc_clockfreq;	/* clock frequency (in Hz) */
	int			sc_nrange;
	struct sbus_rd		*sc_rd;
	int			sc_burst;	/* burst transfer sizes supp. */

	struct resource		*sc_sysio_res;
	int			sc_ign;		/* IGN for this sysio */
	struct iommu_state	sc_is;		/* IOMMU state (iommuvar.h) */

	struct resource		*sc_ot_ires;
	void			*sc_ot_ihand;
	struct resource		*sc_pf_ires;
	void			*sc_pf_ihand;
};

#define	SYSIO_READ8(sc, off)						\
	bus_read_8((sc)->sc_sysio_res, (off))
#define	SYSIO_WRITE8(sc, off, v)					\
	bus_write_8((sc)->sc_sysio_res, (off), (v))

static device_probe_t sbus_probe;
static device_attach_t sbus_attach;
static bus_print_child_t sbus_print_child;
static bus_probe_nomatch_t sbus_probe_nomatch;
static bus_read_ivar_t sbus_read_ivar;
static bus_get_resource_list_t sbus_get_resource_list;
static bus_setup_intr_t sbus_setup_intr;
static bus_alloc_resource_t sbus_alloc_resource;
static bus_activate_resource_t sbus_activate_resource;
static bus_adjust_resource_t sbus_adjust_resource;
static bus_release_resource_t sbus_release_resource;
static bus_get_dma_tag_t sbus_get_dma_tag;
static ofw_bus_get_devinfo_t sbus_get_devinfo;

static int sbus_inlist(const char *, const char *const *);
static struct sbus_devinfo * sbus_setup_dinfo(device_t, struct sbus_softc *,
    phandle_t);
static void sbus_destroy_dinfo(struct sbus_devinfo *);
static void sbus_intr_enable(void *);
static void sbus_intr_disable(void *);
static void sbus_intr_assign(void *);
static void sbus_intr_clear(void *);
static int sbus_find_intrmap(struct sbus_softc *, u_int, bus_addr_t *,
    bus_addr_t *);
static driver_intr_t sbus_overtemp;
static driver_intr_t sbus_pwrfail;
static int sbus_print_res(struct sbus_devinfo *);

static device_method_t sbus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sbus_probe),
	DEVMETHOD(device_attach,	sbus_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	sbus_print_child),
	DEVMETHOD(bus_probe_nomatch,	sbus_probe_nomatch),
	DEVMETHOD(bus_read_ivar,	sbus_read_ivar),
	DEVMETHOD(bus_alloc_resource,	sbus_alloc_resource),
	DEVMETHOD(bus_activate_resource, sbus_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,	sbus_adjust_resource),
	DEVMETHOD(bus_release_resource,	sbus_release_resource),
	DEVMETHOD(bus_setup_intr,	sbus_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_get_resource_list, sbus_get_resource_list),
	DEVMETHOD(bus_child_pnpinfo_str, ofw_bus_gen_child_pnpinfo_str),
	DEVMETHOD(bus_get_dma_tag,	sbus_get_dma_tag),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	sbus_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static driver_t sbus_driver = {
	"sbus",
	sbus_methods,
	sizeof(struct sbus_softc),
};

static devclass_t sbus_devclass;

EARLY_DRIVER_MODULE(sbus, nexus, sbus_driver, sbus_devclass, NULL, NULL,
    BUS_PASS_BUS);
MODULE_DEPEND(sbus, nexus, 1, 1, 1);
MODULE_VERSION(sbus, 1);

#define	OFW_SBUS_TYPE	"sbus"
#define	OFW_SBUS_NAME	"sbus"

static const struct intr_controller sbus_ic = {
	sbus_intr_enable,
	sbus_intr_disable,
	sbus_intr_assign,
	sbus_intr_clear
};

struct sbus_icarg {
	struct sbus_softc	*sica_sc;
	bus_addr_t		sica_map;
	bus_addr_t		sica_clr;
};

static const char *const sbus_order_first[] = {
	"auxio",
	"dma",
	NULL
};

static int
sbus_inlist(const char *name, const char *const *list)
{
	int i;

	if (name == NULL)
		return (0);
	for (i = 0; list[i] != NULL; i++) {
		if (strcmp(name, list[i]) == 0)
			return (1);
	}
	return (0);
}

static int
sbus_probe(device_t dev)
{
	const char *t;

	t = ofw_bus_get_type(dev);
	if (((t == NULL || strcmp(t, OFW_SBUS_TYPE) != 0)) &&
	    strcmp(ofw_bus_get_name(dev), OFW_SBUS_NAME) != 0)
		return (ENXIO);
	device_set_desc(dev, "U2S UPA-SBus bridge");
	return (0);
}

static int
sbus_attach(device_t dev)
{
	struct sbus_softc *sc;
	struct sbus_devinfo *sdi;
	struct sbus_icarg *sica;
	struct sbus_ranges *range;
	struct resource *res;
	struct resource_list *rl;
	device_t cdev;
	bus_addr_t intrclr, intrmap, phys;
	bus_size_t size;
	u_long vec;
	phandle_t child, node;
	uint32_t prop;
	int i, j;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	node = ofw_bus_get_node(dev);

	i = 0;
	sc->sc_sysio_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &i,
	    RF_ACTIVE);
	if (sc->sc_sysio_res == NULL)
		panic("%s: cannot allocate device memory", __func__);

	if (OF_getprop(node, "interrupts", &prop, sizeof(prop)) == -1)
		panic("%s: cannot get IGN", __func__);
	sc->sc_ign = INTIGN(prop);

	/*
	 * Record clock frequency for synchronous SCSI.
	 * IS THIS THE CORRECT DEFAULT??
	 */
	if (OF_getprop(node, "clock-frequency", &prop, sizeof(prop)) == -1)
		prop = 25000000;
	sc->sc_clockfreq = prop;
	prop /= 1000;
	device_printf(dev, "clock %d.%03d MHz\n", prop / 1000, prop % 1000);

	/*
	 * Collect address translations from the OBP.
	 */
	if ((sc->sc_nrange = OF_getprop_alloc_multi(node, "ranges",
	    sizeof(*range), (void **)&range)) == -1) {
		panic("%s: error getting ranges property", __func__);
	}
	sc->sc_rd = malloc(sizeof(*sc->sc_rd) * sc->sc_nrange, M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (sc->sc_rd == NULL)
		panic("%s: cannot allocate rmans", __func__);
	/*
	 * Preallocate all space that the SBus bridge decodes, so that nothing
	 * else gets in the way; set up rmans etc.
	 */
	rl = BUS_GET_RESOURCE_LIST(device_get_parent(dev), dev);
	for (i = 0; i < sc->sc_nrange; i++) {
		phys = range[i].poffset | ((bus_addr_t)range[i].pspace << 32);
		size = range[i].size;
		sc->sc_rd[i].rd_slot = range[i].cspace;
		sc->sc_rd[i].rd_coffset = range[i].coffset;
		sc->sc_rd[i].rd_cend = sc->sc_rd[i].rd_coffset + size;
		j = resource_list_add_next(rl, SYS_RES_MEMORY, phys,
		    phys + size - 1, size);
		if ((res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &j,
		    RF_ACTIVE)) == NULL)
			panic("%s: cannot allocate decoded range", __func__);
		sc->sc_rd[i].rd_bushandle = rman_get_bushandle(res);
		sc->sc_rd[i].rd_rman.rm_type = RMAN_ARRAY;
		sc->sc_rd[i].rd_rman.rm_descr = "SBus Device Memory";
		if (rman_init(&sc->sc_rd[i].rd_rman) != 0 ||
		    rman_manage_region(&sc->sc_rd[i].rd_rman, 0, size) != 0)
			panic("%s: failed to set up memory rman", __func__);
		sc->sc_rd[i].rd_poffset = phys;
		sc->sc_rd[i].rd_pend = phys + size;
		sc->sc_rd[i].rd_res = res;
	}
	OF_prop_free(range);

	/*
	 * Get the SBus burst transfer size if burst transfers are supported.
	 */
	if (OF_getprop(node, "up-burst-sizes", &sc->sc_burst,
	    sizeof(sc->sc_burst)) == -1 || sc->sc_burst == 0)
		sc->sc_burst =
		    (SBUS_BURST64_DEF << SBUS_BURST64_SHIFT) | SBUS_BURST_DEF;

	/* initialise the IOMMU */

	/* punch in our copies */
	sc->sc_is.is_pmaxaddr = IOMMU_MAXADDR(SBUS_IOMMU_BITS);
	sc->sc_is.is_bustag = rman_get_bustag(sc->sc_sysio_res);
	sc->sc_is.is_bushandle = rman_get_bushandle(sc->sc_sysio_res);
	sc->sc_is.is_iommu = SBR_IOMMU;
	sc->sc_is.is_dtag = SBR_IOMMU_TLB_TAG_DIAG;
	sc->sc_is.is_ddram = SBR_IOMMU_TLB_DATA_DIAG;
	sc->sc_is.is_dqueue = SBR_IOMMU_QUEUE_DIAG;
	sc->sc_is.is_dva = SBR_IOMMU_SVADIAG;
	sc->sc_is.is_dtcmp = 0;
	sc->sc_is.is_sb[0] = SBR_STRBUF;
	sc->sc_is.is_sb[1] = 0;

	/*
	 * Note: the SBus IOMMU ignores the high bits of an address, so a NULL
	 * DMA pointer will be translated by the first page of the IOTSB.
	 * To detect bugs we'll allocate and ignore the first entry.
	 */
	iommu_init(device_get_nameunit(dev), &sc->sc_is, 3, -1, 1);

	/* Create the DMA tag. */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 8, 0,
	    sc->sc_is.is_pmaxaddr, ~0, NULL, NULL, sc->sc_is.is_pmaxaddr,
	    0xff, 0xffffffff, 0, NULL, NULL, &sc->sc_cdmatag) != 0)
		panic("%s: bus_dma_tag_create failed", __func__);
	/* Customize the tag. */
	sc->sc_cdmatag->dt_cookie = &sc->sc_is;
	sc->sc_cdmatag->dt_mt = &iommu_dma_methods;

	/*
	 * Hunt through all the interrupt mapping regs and register our
	 * interrupt controller for the corresponding interrupt vectors.
	 * We do this early in order to be able to catch stray interrupts.
	 */
	for (i = 0; i <= SBUS_MAX_INO; i++) {
		if (sbus_find_intrmap(sc, i, &intrmap, &intrclr) == 0)
			continue;
		sica = malloc(sizeof(*sica), M_DEVBUF, M_NOWAIT);
		if (sica == NULL)
			panic("%s: could not allocate interrupt controller "
			    "argument", __func__);
		sica->sica_sc = sc;
		sica->sica_map = intrmap;
		sica->sica_clr = intrclr;
#ifdef SBUS_DEBUG
		device_printf(dev,
		    "intr map (INO %d, %s) %#lx: %#lx, clr: %#lx\n",
		    i, (i & INTMAP_OBIO_MASK) == 0 ? "SBus slot" : "OBIO",
		    (u_long)intrmap, (u_long)SYSIO_READ8(sc, intrmap),
		    (u_long)intrclr);
#endif
		j = intr_controller_register(INTMAP_VEC(sc->sc_ign, i),
		    &sbus_ic, sica);
		if (j != 0)
			device_printf(dev, "could not register interrupt "
			    "controller for INO %d (%d)\n", i, j);
	}

	/* Enable the over-temperature and power-fail interrupts. */
	i = 4;
	sc->sc_ot_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ, &i,
	    RF_ACTIVE);
	if (sc->sc_ot_ires == NULL ||
	    INTIGN(vec = rman_get_start(sc->sc_ot_ires)) != sc->sc_ign ||
	    INTVEC(SYSIO_READ8(sc, SBR_THERM_INT_MAP)) != vec ||
	    intr_vectors[vec].iv_ic != &sbus_ic ||
	    bus_setup_intr(dev, sc->sc_ot_ires, INTR_TYPE_MISC | INTR_BRIDGE | INTR_MPSAFE,
	    NULL, sbus_overtemp, sc, &sc->sc_ot_ihand) != 0)
		panic("%s: failed to set up temperature interrupt", __func__);
	i = 3;
	sc->sc_pf_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ, &i,
	    RF_ACTIVE);
	if (sc->sc_pf_ires == NULL ||
	    INTIGN(vec = rman_get_start(sc->sc_pf_ires)) != sc->sc_ign ||
	    INTVEC(SYSIO_READ8(sc, SBR_POWER_INT_MAP)) != vec ||
	    intr_vectors[vec].iv_ic != &sbus_ic ||
	    bus_setup_intr(dev, sc->sc_pf_ires, INTR_TYPE_MISC | INTR_BRIDGE | INTR_MPSAFE,
	    NULL, sbus_pwrfail, sc, &sc->sc_pf_ihand) != 0)
		panic("%s: failed to set up power fail interrupt", __func__);

	/* Initialize the counter-timer. */
	sparc64_counter_init(device_get_nameunit(dev),
	    rman_get_bustag(sc->sc_sysio_res),
	    rman_get_bushandle(sc->sc_sysio_res), SBR_TC0);

	/*
	 * Loop through ROM children, fixing any relative addresses
	 * and then configuring each device.
	 */
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if ((sdi = sbus_setup_dinfo(dev, sc, child)) == NULL)
			continue;
		/*
		 * For devices where there are variants that are actually
		 * split into two SBus devices (as opposed to the first
		 * half of the device being a SBus device and the second
		 * half hanging off of the first one) like 'auxio' and
		 * 'SUNW,fdtwo' or 'dma' and 'esp' probe the SBus device
		 * which is a prerequisite to the driver attaching to the
		 * second one with a lower order. Saves us from dealing
		 * with different probe orders in the respective device
		 * drivers which generally is more hackish.
		 */
		cdev = device_add_child_ordered(dev, (OF_child(child) == 0 &&
		    sbus_inlist(sdi->sdi_obdinfo.obd_name, sbus_order_first)) ?
		    SBUS_ORDER_FIRST : SBUS_ORDER_NORMAL, NULL, -1);
		if (cdev == NULL) {
			device_printf(dev,
			    "<%s>: device_add_child_ordered failed\n",
			    sdi->sdi_obdinfo.obd_name);
			sbus_destroy_dinfo(sdi);
			continue;
		}
		device_set_ivars(cdev, sdi);
	}
	return (bus_generic_attach(dev));
}

static struct sbus_devinfo *
sbus_setup_dinfo(device_t dev, struct sbus_softc *sc, phandle_t node)
{
	struct sbus_devinfo *sdi;
	struct sbus_regs *reg;
	u_int32_t base, iv, *intr;
	int i, nreg, nintr, slot, rslot;

	sdi = malloc(sizeof(*sdi), M_DEVBUF, M_ZERO | M_WAITOK);
	if (ofw_bus_gen_setup_devinfo(&sdi->sdi_obdinfo, node) != 0) {
		free(sdi, M_DEVBUF);
		return (NULL);
	}
	resource_list_init(&sdi->sdi_rl);
	slot = -1;
	nreg = OF_getprop_alloc_multi(node, "reg", sizeof(*reg), (void **)&reg);
	if (nreg == -1) {
		if (sdi->sdi_obdinfo.obd_type == NULL ||
		    strcmp(sdi->sdi_obdinfo.obd_type, "hierarchical") != 0) {
			device_printf(dev, "<%s>: incomplete\n",
			    sdi->sdi_obdinfo.obd_name);
			goto fail;
		}
	} else {
		for (i = 0; i < nreg; i++) {
			base = reg[i].sbr_offset;
			if (SBUS_ABS(base)) {
				rslot = SBUS_ABS_TO_SLOT(base);
				base = SBUS_ABS_TO_OFFSET(base);
			} else
				rslot = reg[i].sbr_slot;
			if (slot != -1 && slot != rslot) {
				device_printf(dev, "<%s>: multiple slots\n",
				    sdi->sdi_obdinfo.obd_name);
				OF_prop_free(reg);
				goto fail;
			}
			slot = rslot;

			resource_list_add(&sdi->sdi_rl, SYS_RES_MEMORY, i,
			    base, base + reg[i].sbr_size, reg[i].sbr_size);
		}
		OF_prop_free(reg);
	}
	sdi->sdi_slot = slot;

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
			iv = INTMAP_VEC(sc->sc_ign, iv);
			resource_list_add(&sdi->sdi_rl, SYS_RES_IRQ, i,
			    iv, iv, 1);
		}
		OF_prop_free(intr);
	}
	if (OF_getprop(node, "burst-sizes", &sdi->sdi_burstsz,
	    sizeof(sdi->sdi_burstsz)) == -1)
		sdi->sdi_burstsz = sc->sc_burst;
	else
		sdi->sdi_burstsz &= sc->sc_burst;
	if (OF_getprop(node, "clock-frequency", &sdi->sdi_clockfreq,
	    sizeof(sdi->sdi_clockfreq)) == -1)
		sdi->sdi_clockfreq = sc->sc_clockfreq;

	return (sdi);

fail:
	sbus_destroy_dinfo(sdi);
	return (NULL);
}

static void
sbus_destroy_dinfo(struct sbus_devinfo *dinfo)
{

	resource_list_free(&dinfo->sdi_rl);
	ofw_bus_gen_destroy_devinfo(&dinfo->sdi_obdinfo);
	free(dinfo, M_DEVBUF);
}

static int
sbus_print_child(device_t dev, device_t child)
{
	int rv;

	rv = bus_print_child_header(dev, child);
	rv += sbus_print_res(device_get_ivars(child));
	rv += bus_print_child_footer(dev, child);
	return (rv);
}

static void
sbus_probe_nomatch(device_t dev, device_t child)
{
	const char *type;

	device_printf(dev, "<%s>", ofw_bus_get_name(child));
	sbus_print_res(device_get_ivars(child));
	type = ofw_bus_get_type(child);
	printf(" type %s (no driver attached)\n",
	    type != NULL ? type : "unknown");
}

static int
sbus_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct sbus_softc *sc;
	struct sbus_devinfo *dinfo;

	sc = device_get_softc(dev);
	if ((dinfo = device_get_ivars(child)) == NULL)
		return (ENOENT);
	switch (which) {
	case SBUS_IVAR_BURSTSZ:
		*result = dinfo->sdi_burstsz;
		break;
	case SBUS_IVAR_CLOCKFREQ:
		*result = dinfo->sdi_clockfreq;
		break;
	case SBUS_IVAR_IGN:
		*result = sc->sc_ign;
		break;
	case SBUS_IVAR_SLOT:
		*result = dinfo->sdi_slot;
		break;
	default:
		return (ENOENT);
	}
	return (0);
}

static struct resource_list *
sbus_get_resource_list(device_t dev, device_t child)
{
	struct sbus_devinfo *sdi;

	sdi = device_get_ivars(child);
	return (&sdi->sdi_rl);
}

static void
sbus_intr_enable(void *arg)
{
	struct intr_vector *iv = arg;
	struct sbus_icarg *sica = iv->iv_icarg;

	SYSIO_WRITE8(sica->sica_sc, sica->sica_map,
	    INTMAP_ENABLE(iv->iv_vec, iv->iv_mid));
}

static void
sbus_intr_disable(void *arg)
{
	struct intr_vector *iv = arg;
	struct sbus_icarg *sica = iv->iv_icarg;

	SYSIO_WRITE8(sica->sica_sc, sica->sica_map, iv->iv_vec);
}

static void
sbus_intr_assign(void *arg)
{
	struct intr_vector *iv = arg;
	struct sbus_icarg *sica = iv->iv_icarg;

	SYSIO_WRITE8(sica->sica_sc, sica->sica_map, INTMAP_TID(
	    SYSIO_READ8(sica->sica_sc, sica->sica_map), iv->iv_mid));
}

static void
sbus_intr_clear(void *arg)
{
	struct intr_vector *iv = arg;
	struct sbus_icarg *sica = iv->iv_icarg;

	SYSIO_WRITE8(sica->sica_sc, sica->sica_clr, INTCLR_IDLE);
}

static int
sbus_find_intrmap(struct sbus_softc *sc, u_int ino, bus_addr_t *intrmapptr,
    bus_addr_t *intrclrptr)
{
	bus_addr_t intrclr, intrmap;
	int i;

	if (ino > SBUS_MAX_INO) {
		device_printf(sc->sc_dev, "out of range INO %d requested\n",
		    ino);
		return (0);
	}

	if ((ino & INTMAP_OBIO_MASK) == 0) {
		intrmap = SBR_SLOT0_INT_MAP + INTSLOT(ino) * 8;
		intrclr = SBR_SLOT0_INT_CLR +
		    (INTSLOT(ino) * 8 * 8) + (INTPRI(ino) * 8);
	} else {
		intrclr = 0;
		for (i = 0, intrmap = SBR_SCSI_INT_MAP;
		    intrmap <= SBR_RESERVED_INT_MAP; intrmap += 8, i++) {
			if (INTVEC(SYSIO_READ8(sc, intrmap)) ==
			    INTMAP_VEC(sc->sc_ign, ino)) {
				intrclr = SBR_SCSI_INT_CLR + i * 8;
				break;
			}
		}
		if (intrclr == 0)
			return (0);
	}
	if (intrmapptr != NULL)
		*intrmapptr = intrmap;
	if (intrclrptr != NULL)
		*intrclrptr = intrclr;
	return (1);
}

static int
sbus_setup_intr(device_t dev, device_t child, struct resource *ires, int flags,
    driver_filter_t *filt, driver_intr_t *intr, void *arg, void **cookiep)
{
	struct sbus_softc *sc;
	u_long vec;

	sc = device_get_softc(dev);
	/*
	 * Make sure the vector is fully specified and we registered
	 * our interrupt controller for it.
	 */
	vec = rman_get_start(ires);
	if (INTIGN(vec) != sc->sc_ign || intr_vectors[vec].iv_ic != &sbus_ic) {
		device_printf(dev, "invalid interrupt vector 0x%lx\n", vec);
		return (EINVAL);
	}
	return (bus_generic_setup_intr(dev, child, ires, flags, filt, intr,
	    arg, cookiep));
}

static struct resource *
sbus_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct sbus_softc *sc;
	struct rman *rm;
	struct resource *rv;
	struct resource_list *rl;
	struct resource_list_entry *rle;
	device_t schild;
	bus_addr_t toffs;
	bus_size_t tend;
	int i, slot;
	int isdefault, passthrough;

	isdefault = RMAN_IS_DEFAULT_RANGE(start, end);
	passthrough = (device_get_parent(child) != bus);
	rle = NULL;
	sc = device_get_softc(bus);
	rl = BUS_GET_RESOURCE_LIST(bus, child);
	switch (type) {
	case SYS_RES_IRQ:
		return (resource_list_alloc(rl, bus, child, type, rid, start,
		    end, count, flags));
	case SYS_RES_MEMORY:
		if (!passthrough) {
			rle = resource_list_find(rl, type, *rid);
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
		rm = NULL;
		schild = child;
		while (device_get_parent(schild) != bus)
			schild = device_get_parent(schild);
		slot = sbus_get_slot(schild);
		for (i = 0; i < sc->sc_nrange; i++) {
			if (sc->sc_rd[i].rd_slot != slot ||
			    start < sc->sc_rd[i].rd_coffset ||
			    start > sc->sc_rd[i].rd_cend)
				continue;
			/* Disallow cross-range allocations. */
			if (end > sc->sc_rd[i].rd_cend)
				return (NULL);
			/* We've found the connection to the parent bus */
			toffs = start - sc->sc_rd[i].rd_coffset;
			tend = end - sc->sc_rd[i].rd_coffset;
			rm = &sc->sc_rd[i].rd_rman;
			break;
		}
		if (rm == NULL)
			return (NULL);

		rv = rman_reserve_resource(rm, toffs, tend, count, flags &
		    ~RF_ACTIVE, child);
		if (rv == NULL)
			return (NULL);
		rman_set_rid(rv, *rid);

		if ((flags & RF_ACTIVE) != 0 && bus_activate_resource(child,
		    type, *rid, rv)) {
			rman_release_resource(rv);
			return (NULL);
		}
		if (!passthrough)
			rle->res = rv;
		return (rv);
	default:
		return (NULL);
	}
}

static int
sbus_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	struct sbus_softc *sc;
	struct bus_space_tag *tag;
	int i;

	switch (type) {
	case SYS_RES_IRQ:
		return (bus_generic_activate_resource(bus, child, type, rid,
		    r));
	case SYS_RES_MEMORY:
		sc = device_get_softc(bus);
		for (i = 0; i < sc->sc_nrange; i++) {
			if (rman_is_region_manager(r,
			    &sc->sc_rd[i].rd_rman) != 0) {
				tag = sparc64_alloc_bus_tag(r, SBUS_BUS_SPACE);
				if (tag == NULL)
					return (ENOMEM);
				rman_set_bustag(r, tag);
				rman_set_bushandle(r,
				    sc->sc_rd[i].rd_bushandle +
				    rman_get_start(r));
				return (rman_activate_resource(r));
			}
		}
		/* FALLTHROUGH */
	default:
		return (EINVAL);
	}
}

static int
sbus_adjust_resource(device_t bus, device_t child, int type,
    struct resource *r, rman_res_t start, rman_res_t end)
{
	struct sbus_softc *sc;
	int i;

	if (type == SYS_RES_MEMORY) {
		sc = device_get_softc(bus);
		for (i = 0; i < sc->sc_nrange; i++)
			if (rman_is_region_manager(r,
			    &sc->sc_rd[i].rd_rman) != 0)
				return (rman_adjust_resource(r, start, end));
		return (EINVAL);
	}
	return (bus_generic_adjust_resource(bus, child, type, r, start, end));
}

static int
sbus_release_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	struct resource_list *rl;
	struct resource_list_entry *rle;
	int error, passthrough;

	passthrough = (device_get_parent(child) != bus);
	rl = BUS_GET_RESOURCE_LIST(bus, child);
	if (type == SYS_RES_MEMORY) {
		if ((rman_get_flags(r) & RF_ACTIVE) != 0) {
			error = bus_deactivate_resource(child, type, rid, r);
			if (error)
				return (error);
		}
		error = rman_release_resource(r);
		if (error != 0)
			return (error);
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
	return (resource_list_release(rl, bus, child, type, rid, r));
}

static bus_dma_tag_t
sbus_get_dma_tag(device_t bus, device_t child)
{
	struct sbus_softc *sc;

	sc = device_get_softc(bus);
	return (sc->sc_cdmatag);
}

static const struct ofw_bus_devinfo *
sbus_get_devinfo(device_t bus, device_t child)
{
	struct sbus_devinfo *sdi;

	sdi = device_get_ivars(child);
	return (&sdi->sdi_obdinfo);
}

/*
 * Handle an overtemp situation.
 *
 * SPARCs have temperature sensors which generate interrupts
 * if the machine's temperature exceeds a certain threshold.
 * This handles the interrupt and powers off the machine.
 * The same needs to be done to PCI controller drivers.
 */
static void
sbus_overtemp(void *arg __unused)
{
	static int shutdown;

	/* As the interrupt is cleared we may be called multiple times. */
	if (shutdown != 0)
		return;
	shutdown++;
	printf("DANGER: OVER TEMPERATURE detected\nShutting down NOW.\n");
	shutdown_nice(RB_POWEROFF);
}

/* Try to shut down in time in case of power failure. */
static void
sbus_pwrfail(void *arg __unused)
{
	static int shutdown;

	/* As the interrupt is cleared we may be called multiple times. */
	if (shutdown != 0)
		return;
	shutdown++;
	printf("Power failure detected\nShutting down NOW.\n");
	shutdown_nice(RB_POWEROFF);
}

static int
sbus_print_res(struct sbus_devinfo *sdi)
{
	int rv;

	rv = 0;
	rv += resource_list_print_type(&sdi->sdi_rl, "mem", SYS_RES_MEMORY,
	    "%#jx");
	rv += resource_list_print_type(&sdi->sdi_rl, "irq", SYS_RES_IRQ,
	    "%jd");
	return (rv);
}
