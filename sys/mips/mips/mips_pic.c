/*-
 * Copyright (c) 2015 Alexander Kabaev
 * Copyright (c) 2006 Oleksandr Tymoshenko
 * Copyright (c) 2002-2004 Juli Mallett <jmallett@FreeBSD.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"
#include "opt_hwpmc_hooks.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/cpuset.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/sched.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>

#include <machine/bus.h>
#include <machine/hwfunc.h>
#include <machine/intr.h>
#include <machine/smp.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include "pic_if.h"

struct mips_pic_softc;

static int			 mips_pic_intr(void *);
static struct mips_pic_intr	*mips_pic_find_intr(struct resource *r);
static int			 mips_pic_map_fixed_intr(u_int irq,
				     struct mips_pic_intr **mapping);
static void			 cpu_establish_intr(struct mips_pic_softc *sc,
				     const char *name, driver_filter_t *filt,
				     void (*handler)(void*), void *arg, int irq,
				     int flags, void **cookiep);

#define	INTR_MAP_DATA_MIPS	INTR_MAP_DATA_PLAT_1

struct intr_map_data_mips_pic {
	struct intr_map_data	hdr;
	u_int			irq;
};

/**
 * MIPS interrupt state; available prior to MIPS PIC device attachment.
 */
static struct mips_pic_intr {
	u_int			 mips_irq;	/**< MIPS IRQ# 0-7 */
	u_int			 intr_irq;	/**< INTRNG IRQ#, or INTR_IRQ_INVALID if unmapped */
	u_int			 consumers;	/**< INTRNG activation refcount */
	struct resource		*res;		/**< resource shared by all interrupt handlers registered via
						     cpu_establish_hardintr() or cpu_establish_softintr(); NULL
						     if no interrupt handlers are yet registered. */
} mips_pic_intrs[] = {
	{ 0, INTR_IRQ_INVALID, 0, NULL },
	{ 1, INTR_IRQ_INVALID, 0, NULL },
	{ 2, INTR_IRQ_INVALID, 0, NULL },
	{ 3, INTR_IRQ_INVALID, 0, NULL },
	{ 4, INTR_IRQ_INVALID, 0, NULL },
	{ 5, INTR_IRQ_INVALID, 0, NULL },
	{ 6, INTR_IRQ_INVALID, 0, NULL },
	{ 7, INTR_IRQ_INVALID, 0, NULL },
};

struct mtx mips_pic_mtx;
MTX_SYSINIT(mips_pic_mtx, &mips_pic_mtx, "mips intr controller mutex", MTX_DEF);

struct mips_pic_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

struct mips_pic_softc {
	device_t			pic_dev;
	struct mips_pic_irqsrc		pic_irqs[NREAL_IRQS];
	uint32_t			nirqs;
};

static struct mips_pic_softc *pic_sc;

#define PIC_INTR_ISRC(sc, irq)		(&(sc)->pic_irqs[(irq)].isrc)

#ifdef FDT
static struct ofw_compat_data compat_data[] = {
	{"mti,cpu-interrupt-controller",	true},
	{NULL,					false}
};
#endif

#ifndef FDT
static void
mips_pic_identify(driver_t *drv, device_t parent)
{

	BUS_ADD_CHILD(parent, 0, "cpupic", 0);
}
#endif

static int
mips_pic_probe(device_t dev)
{

#ifdef FDT
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);
#endif
	device_set_desc(dev, "MIPS32 Interrupt Controller");
	return (BUS_PROBE_DEFAULT);
}

static inline void
pic_irq_unmask(struct mips_pic_softc *sc, u_int irq)
{

	mips_wr_status(mips_rd_status() | ((1 << irq) << 8));
}

static inline void
pic_irq_mask(struct mips_pic_softc *sc, u_int irq)
{

	mips_wr_status(mips_rd_status() & ~((1 << irq) << 8));
}

static inline intptr_t
pic_xref(device_t dev)
{
#ifdef FDT
	return (OF_xref_from_node(ofw_bus_get_node(dev)));
#else
	return (MIPS_PIC_XREF);
#endif
}

static int
mips_pic_register_isrcs(struct mips_pic_softc *sc)
{
	int error;
	uint32_t irq, i, tmpirq;
	struct intr_irqsrc *isrc;
	char *name;

	for (irq = 0; irq < sc->nirqs; irq++) {
		sc->pic_irqs[irq].irq = irq;

		isrc = PIC_INTR_ISRC(sc, irq);
		if (irq < NSOFT_IRQS) {
			name = "sint";
			tmpirq = irq;
		} else {
			name = "int";
			tmpirq = irq - NSOFT_IRQS;
		}
		error = intr_isrc_register(isrc, sc->pic_dev, 0, "%s%u",
		    name, tmpirq);
		if (error != 0) {
			for (i = 0; i < irq; i++) {
				intr_isrc_deregister(PIC_INTR_ISRC(sc, i));
			}
			device_printf(sc->pic_dev, "%s failed", __func__);
			return (error);
		}
	}

	return (0);
}

static int
mips_pic_attach(device_t dev)
{
	struct		mips_pic_softc *sc;
	intptr_t	xref = pic_xref(dev);

	if (pic_sc)
		return (ENXIO);

	sc = device_get_softc(dev);

	sc->pic_dev = dev;
	pic_sc = sc;

	/* Set the number of interrupts */
	sc->nirqs = nitems(sc->pic_irqs);

	/* Register the interrupts */
	if (mips_pic_register_isrcs(sc) != 0) {
		device_printf(dev, "could not register PIC ISRCs\n");
		goto cleanup;
	}

	/*
	 * Now, when everything is initialized, it's right time to
	 * register interrupt controller to interrupt framefork.
	 */
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "could not register PIC\n");
		goto cleanup;
	}

	/* Claim our root controller role */
	if (intr_pic_claim_root(dev, xref, mips_pic_intr, sc, 0) != 0) {
		device_printf(dev, "could not set PIC as a root\n");
		intr_pic_deregister(dev, xref);
		goto cleanup;
	}

	return (0);

cleanup:
	return(ENXIO);
}

int
mips_pic_intr(void *arg)
{
	struct mips_pic_softc *sc = arg;
	register_t cause, status;
	int i, intr;

	cause = mips_rd_cause();
	status = mips_rd_status();
	intr = (cause & MIPS_INT_MASK) >> 8;
	/*
	 * Do not handle masked interrupts. They were masked by
	 * pre_ithread function (mips_mask_XXX_intr) and will be
	 * unmasked once ithread is through with handler
	 */
	intr &= (status & MIPS_INT_MASK) >> 8;
	while ((i = fls(intr)) != 0) {
		i--; /* Get a 0-offset interrupt. */
		intr &= ~(1 << i);

		if (intr_isrc_dispatch(PIC_INTR_ISRC(sc, i),
		    curthread->td_intr_frame) != 0) {
			device_printf(sc->pic_dev,
			    "Stray interrupt %u detected\n", i);
			pic_irq_mask(sc, i);
			continue;
		}
	}

	KASSERT(i == 0, ("all interrupts handled"));

#ifdef HWPMC_HOOKS
	if (pmc_hook && (PCPU_GET(curthread)->td_pflags & TDP_CALLCHAIN)) {
		struct trapframe *tf = PCPU_GET(curthread)->td_intr_frame;

		pmc_hook(PCPU_GET(curthread), PMC_FN_USER_CALLCHAIN, tf);
	}
#endif
	return (FILTER_HANDLED);
}

static void
mips_pic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;

	irq = ((struct mips_pic_irqsrc *)isrc)->irq;
	pic_irq_mask(device_get_softc(dev), irq);
}

static void
mips_pic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;

	irq = ((struct mips_pic_irqsrc *)isrc)->irq;
	pic_irq_unmask(device_get_softc(dev), irq);
}

static int
mips_pic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct mips_pic_softc *sc;
	int res;

	sc = device_get_softc(dev);
	res = 0;
#ifdef FDT
	if (data->type == INTR_MAP_DATA_FDT) {
		struct intr_map_data_fdt *daf;

		daf = (struct intr_map_data_fdt *)data;

		if (daf->ncells != 1 || daf->cells[0] >= sc->nirqs)
			return (EINVAL);

		*isrcp = PIC_INTR_ISRC(sc, daf->cells[0]);
	} else
#endif
	if (data->type == INTR_MAP_DATA_MIPS) {
		struct intr_map_data_mips_pic *mpd;

		mpd = (struct intr_map_data_mips_pic *)data;

		if (mpd->irq < 0 || mpd->irq >= sc->nirqs)
			return (EINVAL);

		*isrcp = PIC_INTR_ISRC(sc, mpd->irq);
	} else {
		res = ENOTSUP;
	}

	return (res);
}

static void
mips_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	mips_pic_disable_intr(dev, isrc);
}

static void
mips_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	mips_pic_enable_intr(dev, isrc);
}

static void
mips_pic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
}

static device_method_t mips_pic_methods[] = {
	/* Device interface */
#ifndef FDT
	DEVMETHOD(device_identify,	mips_pic_identify),
#endif
	DEVMETHOD(device_probe,		mips_pic_probe),
	DEVMETHOD(device_attach,	mips_pic_attach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	mips_pic_disable_intr),
	DEVMETHOD(pic_enable_intr,	mips_pic_enable_intr),
	DEVMETHOD(pic_map_intr,		mips_pic_map_intr),
	DEVMETHOD(pic_pre_ithread,	mips_pic_pre_ithread),
	DEVMETHOD(pic_post_ithread,	mips_pic_post_ithread),
	DEVMETHOD(pic_post_filter,	mips_pic_post_filter),

	{ 0, 0 }
};

static driver_t mips_pic_driver = {
	"cpupic",
	mips_pic_methods,
	sizeof(struct mips_pic_softc),
};

static devclass_t mips_pic_devclass;

#ifdef FDT
EARLY_DRIVER_MODULE(cpupic, ofwbus, mips_pic_driver, mips_pic_devclass, 0, 0,
    BUS_PASS_INTERRUPT);
#else
EARLY_DRIVER_MODULE(cpupic, nexus, mips_pic_driver, mips_pic_devclass, 0, 0,
    BUS_PASS_INTERRUPT);
#endif

/**
 * Return the MIPS interrupt map entry for @p r, or NULL if no such entry has
 * been created.
 */
static struct mips_pic_intr *
mips_pic_find_intr(struct resource *r)
{
	struct mips_pic_intr	*intr;
	rman_res_t		 irq;

	irq = rman_get_start(r);
	if (irq != rman_get_end(r) || rman_get_size(r) != 1)
		return (NULL);

	mtx_lock(&mips_pic_mtx);
	for (size_t i = 0; i < nitems(mips_pic_intrs); i++) {
		intr = &mips_pic_intrs[i];

		if (intr->intr_irq != irq)
			continue;

		mtx_unlock(&mips_pic_mtx);
		return (intr);
	}
	mtx_unlock(&mips_pic_mtx);

	/* Not found */
	return (NULL);
}

/**
 * Allocate a fixed IRQ mapping for the given MIPS @p irq, or return the
 * existing mapping if @p irq was previously mapped.
 * 
 * @param	irq	The MIPS IRQ to be mapped.
 * @param[out]	mapping	On success, will be populated with the interrupt
 *			mapping.
 *
 * @retval 0		success
 * @retval EINVAL	if @p irq is not a valid MIPS IRQ#.
 * @retval non-zero	If allocating the MIPS IRQ mapping otherwise fails, a
 *			regular unix error code will be returned.
 */
static int
mips_pic_map_fixed_intr(u_int irq, struct mips_pic_intr **mapping)
{
	struct mips_pic_intr		*intr;
	struct intr_map_data_mips_pic	*data;
	device_t			 pic_dev;
	uintptr_t			 xref;

	if (irq < 0 || irq >= nitems(mips_pic_intrs))
		return (EINVAL);

	mtx_lock(&mips_pic_mtx);

	/* Fetch corresponding interrupt entry */
	intr = &mips_pic_intrs[irq];
	KASSERT(intr->mips_irq == irq,
		("intr %u found at index %u", intr->mips_irq, irq));

	/* Already mapped? */
	if (intr->intr_irq != INTR_IRQ_INVALID) {
		mtx_unlock(&mips_pic_mtx);
		*mapping = intr;
		return (0);
	}

	/* Map the interrupt */
	data = (struct intr_map_data_mips_pic *)intr_alloc_map_data(
		INTR_MAP_DATA_MIPS, sizeof(*data), M_WAITOK | M_ZERO);
	data->irq = intr->mips_irq;

#ifdef FDT
	/* PIC must be attached on FDT devices */
	KASSERT(pic_sc != NULL, ("%s: no pic", __func__));

	pic_dev = pic_sc->pic_dev;
	xref = pic_xref(pic_dev);
#else /* !FDT */
	/* PIC has a fixed xref, and may not have been attached yet */
	pic_dev = NULL;
	if (pic_sc != NULL)
		pic_dev = pic_sc->pic_dev;

	xref = MIPS_PIC_XREF;
#endif /* FDT */

	KASSERT(intr->intr_irq == INTR_IRQ_INVALID, ("duplicate map"));
	intr->intr_irq = intr_map_irq(pic_dev, xref, &data->hdr);
	*mapping = intr;

	mtx_unlock(&mips_pic_mtx);
	return (0);
}

/**
 * 
 * Produce fixed IRQ mappings for all MIPS IRQs.
 *
 * Non-FDT/OFW MIPS targets do not provide an equivalent to OFW_BUS_MAP_INTR();
 * it is instead necessary to reserve INTRNG IRQ# 0-7 for use by MIPS device
 * drivers that assume INTRNG IRQs 0-7 are directly mapped to MIPS IRQs 0-7.
 * 
 * XXX: There is no support in INTRNG for reserving a fixed IRQ range. However,
 * we should be called prior to any other interrupt mapping requests, and work
 * around this by iteratively allocating the required 0-7 MIP IRQ# range.
 *
 * @retval 0		success
 * @retval non-zero	If allocating the MIPS IRQ mappings otherwise fails, a
 *			regular unix error code will be returned.
 */
int
mips_pic_map_fixed_intrs(void)
{
	int error;

	for (u_int i = 0; i < nitems(mips_pic_intrs); i++) {
		struct mips_pic_intr *intr;

		if ((error = mips_pic_map_fixed_intr(i, &intr)))
			return (error);

		/* INTRNG IRQs 0-7 must be directly mapped to MIPS IRQs 0-7 */
		if (intr->intr_irq != intr->mips_irq) {
			panic("invalid IRQ mapping: %u->%u", intr->intr_irq,
			    intr->mips_irq);
		}
	}

	return (0);
}

/**
 * If @p r references a MIPS interrupt mapped by the MIPS32 interrupt
 * controller, handle interrupt activation internally.
 *
 * Otherwise, delegate directly to intr_activate_irq().
 */
int
mips_pic_activate_intr(device_t child, struct resource *r)
{
	struct mips_pic_intr	*intr;
	int			 error;

	/* Is this one of our shared MIPS interrupts? */
	if ((intr = mips_pic_find_intr(r)) == NULL) {
		/* Delegate to standard INTRNG activation */
		return (intr_activate_irq(child, r));
	}

	/* Bump consumer count and request activation if required */
	mtx_lock(&mips_pic_mtx);
	if (intr->consumers == UINT_MAX) {
		mtx_unlock(&mips_pic_mtx);
		return (ENOMEM);
	}

	if (intr->consumers == 0) {
		if ((error = intr_activate_irq(child, r))) {
			mtx_unlock(&mips_pic_mtx);
			return (error);
		}
	}

	intr->consumers++;
	mtx_unlock(&mips_pic_mtx);

	return (0);
}

/**
 * If @p r references a MIPS interrupt mapped by the MIPS32 interrupt
 * controller, handle interrupt deactivation internally.
 * 
 * Otherwise, delegate directly to intr_deactivate_irq().
 */
int
mips_pic_deactivate_intr(device_t child, struct resource *r)
{
	struct mips_pic_intr	*intr;
	int			 error;

	/* Is this one of our shared MIPS interrupts? */
	if ((intr = mips_pic_find_intr(r)) == NULL) {
		/* Delegate to standard INTRNG deactivation */
		return (intr_deactivate_irq(child, r));
	}

	/* Decrement consumer count and request deactivation if required */
	mtx_lock(&mips_pic_mtx);
	KASSERT(intr->consumers > 0, ("refcount overrelease"));

	if (intr->consumers == 1) {
		if ((error = intr_deactivate_irq(child, r))) {
			mtx_unlock(&mips_pic_mtx);
			return (error);
		}
	}
	intr->consumers--;
	
	mtx_unlock(&mips_pic_mtx);
	return (0);
}

void
cpu_init_interrupts(void)
{
}

/**
 * Provide backwards-compatible support for registering a MIPS interrupt handler
 * directly, without allocating a bus resource. 
 */
static void
cpu_establish_intr(struct mips_pic_softc *sc, const char *name,
    driver_filter_t *filt, void (*handler)(void*), void *arg, int irq,
    int flags, void **cookiep)
{
	struct mips_pic_intr	*intr;
	struct resource		*res;
	int			 rid;
	int			 error;

	rid = -1;

	/* Fetch (or create) a fixed mapping */
	if ((error = mips_pic_map_fixed_intr(irq, &intr)))
		panic("Unable to map IRQ %d: %d", irq, error);

	/* Fetch the backing resource, if any */
	mtx_lock(&mips_pic_mtx);
	res = intr->res;
	mtx_unlock(&mips_pic_mtx);

	/* Allocate our IRQ resource */
	if (res == NULL) {
		/* Optimistically perform resource allocation */
		rid = intr->intr_irq;
		res = bus_alloc_resource(sc->pic_dev, SYS_RES_IRQ, &rid,
		    intr->intr_irq, intr->intr_irq, 1, RF_SHAREABLE|RF_ACTIVE);

		if (res != NULL) {
			/* Try to update intr->res */
			mtx_lock(&mips_pic_mtx);
			if (intr->res == NULL) {
				intr->res = res;
			}
			mtx_unlock(&mips_pic_mtx);

			/* If intr->res was updated concurrently, free our local
			 * resource allocation */
			if (intr->res != res) {
				bus_release_resource(sc->pic_dev, SYS_RES_IRQ,
				    rid, res);
			}
		} else {
			/* Maybe someone else allocated it? */
			mtx_lock(&mips_pic_mtx);
			res = intr->res;
			mtx_unlock(&mips_pic_mtx);
		}
	
		if (res == NULL) {
			panic("Unable to allocate IRQ %d->%u resource", irq,
			    intr->intr_irq);
		}
	}

	error = bus_setup_intr(sc->pic_dev, res, flags, filt, handler, arg,
	    cookiep);
	if (error)
		panic("Unable to add IRQ %d handler: %d", irq, error);
}

void
cpu_establish_hardintr(const char *name, driver_filter_t *filt,
    void (*handler)(void*), void *arg, int irq, int flags, void **cookiep)
{
	KASSERT(pic_sc != NULL, ("%s: no pic", __func__));

	if (irq < 0 || irq >= NHARD_IRQS)
		panic("%s called for unknown hard intr %d", __func__, irq);

	cpu_establish_intr(pic_sc, name, filt, handler, arg, irq+NSOFT_IRQS,
	    flags, cookiep);
}

void
cpu_establish_softintr(const char *name, driver_filter_t *filt,
    void (*handler)(void*), void *arg, int irq, int flags,
    void **cookiep)
{
	KASSERT(pic_sc != NULL, ("%s: no pic", __func__));

	if (irq < 0 || irq >= NSOFT_IRQS)
		panic("%s called for unknown soft intr %d", __func__, irq);

	cpu_establish_intr(pic_sc, name, filt, handler, arg, irq, flags,
	    cookiep);
}

