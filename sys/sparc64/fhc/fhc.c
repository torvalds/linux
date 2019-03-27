/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Jake Burkholder.
 * Copyright (c) 2005 Marius Strobl <marius@FreeBSD.org>
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
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/pcpu.h>

#include <dev/led/led.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/bus_common.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <sparc64/fhc/fhcreg.h>
#include <sparc64/sbus/ofw_sbus.h>

struct fhc_devinfo {
	struct ofw_bus_devinfo	fdi_obdinfo;
	struct resource_list	fdi_rl;
};

struct fhc_softc {
	struct resource		*sc_memres[FHC_NREG];
	int			sc_nrange;
	struct sbus_ranges	*sc_ranges;
	int			sc_ign;
	struct cdev		*sc_led_dev;
};

static device_probe_t fhc_probe;
static device_attach_t fhc_attach;
static bus_print_child_t fhc_print_child;
static bus_probe_nomatch_t fhc_probe_nomatch;
static bus_setup_intr_t fhc_setup_intr;
static bus_alloc_resource_t fhc_alloc_resource;
static bus_adjust_resource_t fhc_adjust_resource;
static bus_get_resource_list_t fhc_get_resource_list;
static ofw_bus_get_devinfo_t fhc_get_devinfo;

static void fhc_intr_enable(void *);
static void fhc_intr_disable(void *);
static void fhc_intr_assign(void *);
static void fhc_intr_clear(void *);
static void fhc_led_func(void *, int);
static int fhc_print_res(struct fhc_devinfo *);

static device_method_t fhc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fhc_probe),
	DEVMETHOD(device_attach,	fhc_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	fhc_print_child),
	DEVMETHOD(bus_probe_nomatch,	fhc_probe_nomatch),
	DEVMETHOD(bus_alloc_resource,	fhc_alloc_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,	fhc_adjust_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_rl_release_resource),
	DEVMETHOD(bus_setup_intr,	fhc_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_get_resource_list, fhc_get_resource_list),
	DEVMETHOD(bus_child_pnpinfo_str, ofw_bus_gen_child_pnpinfo_str),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	fhc_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static driver_t fhc_driver = {
	"fhc",
	fhc_methods,
	sizeof(struct fhc_softc),
};

static devclass_t fhc_devclass;

EARLY_DRIVER_MODULE(fhc, central, fhc_driver, fhc_devclass, 0, 0,
    BUS_PASS_BUS);
MODULE_DEPEND(fhc, central, 1, 1, 1);
EARLY_DRIVER_MODULE(fhc, nexus, fhc_driver, fhc_devclass, 0, 0,
    BUS_PASS_BUS);
MODULE_DEPEND(fhc, nexus, 1, 1, 1);
MODULE_VERSION(fhc, 1);

static const struct intr_controller fhc_ic = {
	fhc_intr_enable,
	fhc_intr_disable,
	fhc_intr_assign,
	fhc_intr_clear
};

struct fhc_icarg {
	struct fhc_softc	*fica_sc;
	struct resource		*fica_memres;
};

static int
fhc_probe(device_t dev)
{

	if (strcmp(ofw_bus_get_name(dev), "fhc") == 0) {
		device_set_desc(dev, "fhc");
		return (0);
	}
	return (ENXIO);
}

static int
fhc_attach(device_t dev)
{
	char ledname[sizeof("boardXX")];
	struct fhc_devinfo *fdi;
	struct fhc_icarg *fica;
	struct fhc_softc *sc;
	struct sbus_regs *reg;
	phandle_t child;
	phandle_t node;
	device_t cdev;
	uint32_t board;
	uint32_t ctrl;
	uint32_t *intr;
	uint32_t iv;
	char *name;
	int central;
	int error;
	int i;
	int j;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

	central = 0;
	if (strcmp(device_get_name(device_get_parent(dev)), "central") == 0)
		central = 1;

	for (i = 0; i < FHC_NREG; i++) {
		j = i;
		sc->sc_memres[i] = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
		    &j, RF_ACTIVE);
		if (sc->sc_memres[i] == NULL) {
			device_printf(dev, "cannot allocate resource %d\n", i);
			error = ENXIO;
			goto fail_memres;
		}
	}

	if (central != 0) {
		board = bus_read_4(sc->sc_memres[FHC_INTERNAL], FHC_BSR);
		board = ((board >> 16) & 0x1) | ((board >> 12) & 0xe);
	} else {
		if (OF_getprop(node, "board#", &board, sizeof(board)) == -1) {
			device_printf(dev, "cannot get board number\n");
			error = ENXIO;
			goto fail_memres;
		}
	}

	device_printf(dev, "board %d, ", board);
	if (OF_getprop_alloc(node, "board-model", (void **)&name) != -1) {
		printf("model %s\n", name);
		OF_prop_free(name);
	} else
		printf("model unknown\n");

	for (i = FHC_FANFAIL; i <= FHC_TOD; i++) {
		bus_write_4(sc->sc_memres[i], FHC_ICLR, INTCLR_IDLE);
		(void)bus_read_4(sc->sc_memres[i], FHC_ICLR);
	}

	sc->sc_ign = board << 1;
	bus_write_4(sc->sc_memres[FHC_IGN], 0x0, sc->sc_ign);
	sc->sc_ign = bus_read_4(sc->sc_memres[FHC_IGN], 0x0);

	ctrl = bus_read_4(sc->sc_memres[FHC_INTERNAL], FHC_CTRL);
	if (central == 0)
		ctrl |= FHC_CTRL_IXIST;
	ctrl &= ~(FHC_CTRL_AOFF | FHC_CTRL_BOFF | FHC_CTRL_SLINE);
	bus_write_4(sc->sc_memres[FHC_INTERNAL], FHC_CTRL, ctrl);
	(void)bus_read_4(sc->sc_memres[FHC_INTERNAL], FHC_CTRL);

	sc->sc_nrange = OF_getprop_alloc_multi(node, "ranges",
	    sizeof(*sc->sc_ranges), (void **)&sc->sc_ranges);
	if (sc->sc_nrange == -1) {
		device_printf(dev, "cannot get ranges\n");
		error = ENXIO;
		goto fail_memres;
	}

	/*
	 * Apparently only the interrupt controller of boards hanging off
	 * of central(4) is indented to be used, otherwise we would have
	 * conflicts registering the interrupt controllers for all FHC
	 * boards as the board number and thus the IGN isn't unique.
	 */
	if (central == 1) {
		/*
		 * Hunt through all the interrupt mapping regs and register
		 * our interrupt controller for the corresponding interrupt
		 * vectors.  We do this early in order to be able to catch
		 * stray interrupts.
		 */
		for (i = FHC_FANFAIL; i <= FHC_TOD; i++) {
			fica = malloc(sizeof(*fica), M_DEVBUF, M_NOWAIT);
			if (fica == NULL)
				panic("%s: could not allocate interrupt "
				    "controller argument", __func__);
			fica->fica_sc = sc;
			fica->fica_memres = sc->sc_memres[i];
#ifdef FHC_DEBUG
			device_printf(dev, "intr map %d: %#lx, clr: %#lx\n", i,
			    (u_long)bus_read_4(fica->fica_memres, FHC_IMAP),
			    (u_long)bus_read_4(fica->fica_memres, FHC_ICLR));
#endif
			/*
			 * XXX we only pick the INO rather than the INR
			 * from the IMR since the firmware may not provide
			 * the IGN and the IGN is constant for all devices
			 * on that FireHose controller.
			 */
			j = intr_controller_register(INTMAP_VEC(sc->sc_ign,
			    INTINO(bus_read_4(fica->fica_memres, FHC_IMAP))),
			    &fhc_ic, fica);
			if (j != 0)
				device_printf(dev, "could not register "
				    "interrupt controller for map %d (%d)\n",
				    i, j);
		}
	} else {
		snprintf(ledname, sizeof(ledname), "board%d", board);
		sc->sc_led_dev = led_create(fhc_led_func, sc, ledname);
	}

	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		fdi = malloc(sizeof(*fdi), M_DEVBUF, M_WAITOK | M_ZERO);
		if (ofw_bus_gen_setup_devinfo(&fdi->fdi_obdinfo, child) != 0) {
			free(fdi, M_DEVBUF);
			continue;
		}
		i = OF_getprop_alloc_multi(child, "reg", sizeof(*reg),
		    (void **)&reg);
		if (i == -1) {
			device_printf(dev, "<%s>: incomplete\n",
			    fdi->fdi_obdinfo.obd_name);
			ofw_bus_gen_destroy_devinfo(&fdi->fdi_obdinfo);
			free(fdi, M_DEVBUF);
			continue;
		}
		resource_list_init(&fdi->fdi_rl);
		for (j = 0; j < i; j++)
			resource_list_add(&fdi->fdi_rl, SYS_RES_MEMORY, j,
			    reg[j].sbr_offset, reg[j].sbr_offset +
			    reg[j].sbr_size, reg[j].sbr_size);
		OF_prop_free(reg);
		if (central == 1) {
			i = OF_getprop_alloc_multi(child, "interrupts",
			    sizeof(*intr), (void **)&intr);
			if (i != -1) {
				for (j = 0; j < i; j++) {
					iv = INTMAP_VEC(sc->sc_ign, intr[j]);
					resource_list_add(&fdi->fdi_rl,
					    SYS_RES_IRQ, j, iv, iv, 1);
				}
				OF_prop_free(intr);
			}
		}
		cdev = device_add_child(dev, NULL, -1);
		if (cdev == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
			    fdi->fdi_obdinfo.obd_name);
			resource_list_free(&fdi->fdi_rl);
			ofw_bus_gen_destroy_devinfo(&fdi->fdi_obdinfo);
			free(fdi, M_DEVBUF);
			continue;
		}
		device_set_ivars(cdev, fdi);
	}

	return (bus_generic_attach(dev));

 fail_memres:
	for (i = 0; i < FHC_NREG; i++)
		if (sc->sc_memres[i] != NULL)
			bus_release_resource(dev, SYS_RES_MEMORY,
			    rman_get_rid(sc->sc_memres[i]), sc->sc_memres[i]);
	return (error);
}

static int
fhc_print_child(device_t dev, device_t child)
{
	int rv;

	rv = bus_print_child_header(dev, child);
	rv += fhc_print_res(device_get_ivars(child));
	rv += bus_print_child_footer(dev, child);
	return (rv);
}

static void
fhc_probe_nomatch(device_t dev, device_t child)
{
	const char *type;

	device_printf(dev, "<%s>", ofw_bus_get_name(child));
	fhc_print_res(device_get_ivars(child));
	type = ofw_bus_get_type(child);
	printf(" type %s (no driver attached)\n",
	    type != NULL ? type : "unknown");
}

static void
fhc_intr_enable(void *arg)
{
	struct intr_vector *iv = arg;
	struct fhc_icarg *fica = iv->iv_icarg;

	bus_write_4(fica->fica_memres, FHC_IMAP,
	    INTMAP_ENABLE(iv->iv_vec, iv->iv_mid));
	(void)bus_read_4(fica->fica_memres, FHC_IMAP);
}

static void
fhc_intr_disable(void *arg)
{
	struct intr_vector *iv = arg;
	struct fhc_icarg *fica = iv->iv_icarg;

	bus_write_4(fica->fica_memres, FHC_IMAP, iv->iv_vec);
	(void)bus_read_4(fica->fica_memres, FHC_IMAP);
}

static void
fhc_intr_assign(void *arg)
{
	struct intr_vector *iv = arg;
	struct fhc_icarg *fica = iv->iv_icarg;

	bus_write_4(fica->fica_memres, FHC_IMAP, INTMAP_TID(
	    bus_read_4(fica->fica_memres, FHC_IMAP), iv->iv_mid));
	(void)bus_read_4(fica->fica_memres, FHC_IMAP);
}

static void
fhc_intr_clear(void *arg)
{
	struct intr_vector *iv = arg;
	struct fhc_icarg *fica = iv->iv_icarg;

	bus_write_4(fica->fica_memres, FHC_ICLR, INTCLR_IDLE);
	(void)bus_read_4(fica->fica_memres, FHC_ICLR);
}

static int
fhc_setup_intr(device_t bus, device_t child, struct resource *r, int flags,
    driver_filter_t *filt, driver_intr_t *func, void *arg, void **cookiep)
{
	struct fhc_softc *sc;
	u_long vec;

	sc = device_get_softc(bus);
	/*
	 * Make sure the vector is fully specified and we registered
	 * our interrupt controller for it.
	 */
	vec = rman_get_start(r);
	if (INTIGN(vec) != sc->sc_ign || intr_vectors[vec].iv_ic != &fhc_ic) {
		device_printf(bus, "invalid interrupt vector 0x%lx\n", vec);
		return (EINVAL);
	}
	return (bus_generic_setup_intr(bus, child, r, flags, filt, func,
	    arg, cookiep));
}

static struct resource *
fhc_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource_list *rl;
	struct resource_list_entry *rle;
	struct fhc_softc *sc;
	struct resource *res;
	bus_addr_t coffset;
	bus_addr_t cend;
	bus_addr_t phys;
	int isdefault;
	int passthrough;
	int i;

	isdefault = RMAN_IS_DEFAULT_RANGE(start, end);
	passthrough = (device_get_parent(child) != bus);
	res = NULL;
	rle = NULL;
	rl = BUS_GET_RESOURCE_LIST(bus, child);
	sc = device_get_softc(bus);
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
		for (i = 0; i < sc->sc_nrange; i++) {
			coffset = sc->sc_ranges[i].coffset;
			cend = coffset + sc->sc_ranges[i].size - 1;
			if (start >= coffset && end <= cend) {
				start -= coffset;
				end -= coffset;
				phys = sc->sc_ranges[i].poffset |
				    ((bus_addr_t)sc->sc_ranges[i].pspace << 32);
				res = bus_generic_alloc_resource(bus, child,
				    type, rid, phys + start, phys + end,
				    count, flags);
				if (!passthrough)
					rle->res = res;
				break;
			}
		}
		break;
	}
	return (res);
}

static int
fhc_adjust_resource(device_t bus __unused, device_t child __unused,
    int type __unused, struct resource *r __unused, rman_res_t start __unused,
    rman_res_t end __unused)
{

	return (ENXIO);
}

static struct resource_list *
fhc_get_resource_list(device_t bus, device_t child)
{
	struct fhc_devinfo *fdi;

	fdi = device_get_ivars(child);
	return (&fdi->fdi_rl);
}

static const struct ofw_bus_devinfo *
fhc_get_devinfo(device_t bus, device_t child)
{
	struct fhc_devinfo *fdi;

	fdi = device_get_ivars(child);
	return (&fdi->fdi_obdinfo);
}

static void
fhc_led_func(void *arg, int onoff)
{
	struct fhc_softc *sc;
	uint32_t ctrl;

	sc = (struct fhc_softc *)arg;

	ctrl = bus_read_4(sc->sc_memres[FHC_INTERNAL], FHC_CTRL);
	if (onoff)
		ctrl |= FHC_CTRL_RLED;
	else
		ctrl &= ~FHC_CTRL_RLED;
	ctrl &= ~(FHC_CTRL_AOFF | FHC_CTRL_BOFF | FHC_CTRL_SLINE);
	bus_write_4(sc->sc_memres[FHC_INTERNAL], FHC_CTRL, ctrl);
	(void)bus_read_4(sc->sc_memres[FHC_INTERNAL], FHC_CTRL);
}

static int
fhc_print_res(struct fhc_devinfo *fdi)
{
	int rv;

	rv = 0;
	rv += resource_list_print_type(&fdi->fdi_rl, "mem", SYS_RES_MEMORY,
	    "%#jx");
	rv += resource_list_print_type(&fdi->fdi_rl, "irq", SYS_RES_IRQ, "%jd");
	return (rv);
}
