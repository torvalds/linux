/*-
 * Copyright (c) 2016, Hiroki Mori
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#include "opt_platform.h"
#include "opt_ar531x.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/malloc.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>

#include <machine/bus.h>
#ifdef INTRNG
#include <machine/intr.h>
#else
#include <machine/intr_machdep.h>
#endif

#ifdef INTRNG
#include "pic_if.h"

#define PIC_INTR_ISRC(sc, irq)	(&(sc)->pic_irqs[(irq)].isrc)
#endif

#include <mips/atheros/ar531x/apbvar.h>
#include <mips/atheros/ar531x/ar5315reg.h>
#include <mips/atheros/ar531x/ar5312reg.h>
#include <mips/atheros/ar531x/ar5315_setup.h>

#ifdef AR531X_APB_DEBUG
#define dprintf printf
#else 
#define dprintf(x, arg...)
#endif  /* AR531X_APB_DEBUG */

static int	apb_activate_resource(device_t, device_t, int, int,
		    struct resource *);
static device_t	apb_add_child(device_t, u_int, const char *, int);
static struct resource *
		apb_alloc_resource(device_t, device_t, int, int *, rman_res_t,
		    rman_res_t, rman_res_t, u_int);
static int	apb_attach(device_t);
static int	apb_deactivate_resource(device_t, device_t, int, int,
		    struct resource *);
static struct resource_list *
		apb_get_resource_list(device_t, device_t);
static void	apb_hinted_child(device_t, const char *, int);
static int	apb_filter(void *);
static int	apb_probe(device_t);
static int	apb_release_resource(device_t, device_t, int, int,
		    struct resource *);
#ifndef INTRNG
static int	apb_setup_intr(device_t, device_t, struct resource *, int,
		    driver_filter_t *, driver_intr_t *, void *, void **);
static int	apb_teardown_intr(device_t, device_t, struct resource *,
		    void *);
#endif

static void 
apb_mask_irq(void *source)
{
	unsigned int irq = (unsigned int)source;
	uint32_t reg;

	if(ar531x_soc >= AR531X_SOC_AR5315) {
		reg = ATH_READ_REG(AR5315_SYSREG_BASE +
			AR5315_SYSREG_MISC_INTMASK);
		ATH_WRITE_REG(AR5315_SYSREG_BASE
			+ AR5315_SYSREG_MISC_INTMASK, reg & ~(1 << irq));
	} else {
		reg = ATH_READ_REG(AR5312_SYSREG_BASE +
			AR5312_SYSREG_MISC_INTMASK);
		ATH_WRITE_REG(AR5312_SYSREG_BASE
			+ AR5312_SYSREG_MISC_INTMASK, reg & ~(1 << irq));
	}
}

static void 
apb_unmask_irq(void *source)
{
	uint32_t reg;
	unsigned int irq = (unsigned int)source;

	if(ar531x_soc >= AR531X_SOC_AR5315) {
		reg = ATH_READ_REG(AR5315_SYSREG_BASE +
			AR5315_SYSREG_MISC_INTMASK);
		ATH_WRITE_REG(AR5315_SYSREG_BASE +
			AR5315_SYSREG_MISC_INTMASK, reg | (1 << irq));
	} else {
		reg = ATH_READ_REG(AR5312_SYSREG_BASE +
			AR5312_SYSREG_MISC_INTMASK);
		ATH_WRITE_REG(AR5312_SYSREG_BASE +
			AR5312_SYSREG_MISC_INTMASK, reg | (1 << irq));
	}
}

#ifdef INTRNG
static int
apb_pic_register_isrcs(struct apb_softc *sc)
{
	int error;
	uint32_t irq;
	struct intr_irqsrc *isrc;
	const char *name;

	name = device_get_nameunit(sc->apb_dev);
	for (irq = 0; irq < APB_NIRQS; irq++) {
		sc->pic_irqs[irq].irq = irq;
		isrc = PIC_INTR_ISRC(sc, irq);
		error = intr_isrc_register(isrc, sc->apb_dev, 0, "%s", name);
		if (error != 0) {
			/* XXX call intr_isrc_deregister */
			device_printf(sc->apb_dev, "%s failed", __func__);
			return (error);
		}
	}

	return (0);
}

static inline intptr_t
pic_xref(device_t dev)
{
        return (0);
}
#endif

static int
apb_probe(device_t dev)
{
#ifdef INTRNG
	device_set_desc(dev, "APB Bus bridge INTRNG");
#else
	device_set_desc(dev, "APB Bus bridge");
#endif

	return (0);
}

static int
apb_attach(device_t dev)
{
	struct apb_softc *sc = device_get_softc(dev);
#ifdef INTRNG
	intptr_t xref = pic_xref(dev);
	int miscirq;
#else
	int rid = 0;
#endif

	sc->apb_dev = dev;

	sc->apb_mem_rman.rm_type = RMAN_ARRAY;
	sc->apb_mem_rman.rm_descr = "APB memory window";

	if(ar531x_soc >= AR531X_SOC_AR5315) {
		if (rman_init(&sc->apb_mem_rman) != 0 ||
		    rman_manage_region(&sc->apb_mem_rman, 
			AR5315_APB_BASE, 
			AR5315_APB_BASE + AR5315_APB_SIZE - 1) != 0)
			panic("apb_attach: failed to set up memory rman");
	} else {
		if (rman_init(&sc->apb_mem_rman) != 0 ||
		    rman_manage_region(&sc->apb_mem_rman, 
			AR5312_APB_BASE, 
			AR5312_APB_BASE + AR5312_APB_SIZE - 1) != 0)
			panic("apb_attach: failed to set up memory rman");
	}

	sc->apb_irq_rman.rm_type = RMAN_ARRAY;
	sc->apb_irq_rman.rm_descr = "APB IRQ";

	if (rman_init(&sc->apb_irq_rman) != 0 ||
	    rman_manage_region(&sc->apb_irq_rman, 
			APB_IRQ_BASE, APB_IRQ_END) != 0)
		panic("apb_attach: failed to set up IRQ rman");

#ifndef INTRNG
	if ((sc->sc_misc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, 
	    RF_SHAREABLE | RF_ACTIVE)) == NULL) {
		device_printf(dev, "unable to allocate IRQ resource\n");
		return (ENXIO);
	}

	if ((bus_setup_intr(dev, sc->sc_misc_irq, INTR_TYPE_MISC, 
	    apb_filter, NULL, sc, &sc->sc_misc_ih))) {
		device_printf(dev,
		    "WARNING: unable to register interrupt handler\n");
		return (ENXIO);
	}
#else
	/* Register the interrupts */
	if (apb_pic_register_isrcs(sc) != 0) {
		device_printf(dev, "could not register PIC ISRCs\n");
		return (ENXIO);
	}

	/*
	 * Now, when everything is initialized, it's right time to
	 * register interrupt controller to interrupt framefork.
	 */
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "could not register PIC\n");
		return (ENXIO);
	}

	if(ar531x_soc >= AR531X_SOC_AR5315) {
		miscirq = AR5315_CPU_IRQ_MISC;
	} else {
		miscirq = AR5312_IRQ_MISC;
	}
	cpu_establish_hardintr("aric", apb_filter, NULL, sc, miscirq,
	    INTR_TYPE_MISC, NULL);
#endif

	/* mask all misc interrupt */
	if(ar531x_soc >= AR531X_SOC_AR5315) {
		ATH_WRITE_REG(AR5315_SYSREG_BASE
			+ AR5315_SYSREG_MISC_INTMASK, 0);
	} else {
		ATH_WRITE_REG(AR5312_SYSREG_BASE
			+ AR5312_SYSREG_MISC_INTMASK, 0);
	}

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	bus_generic_attach(dev);

	return (0);
}

static struct resource *
apb_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct apb_softc		*sc = device_get_softc(bus);
	struct apb_ivar			*ivar = device_get_ivars(child);
	struct resource			*rv;
	struct resource_list_entry	*rle;
	struct rman			*rm;
	int				 isdefault, needactivate, passthrough;

	isdefault = (RMAN_IS_DEFAULT_RANGE(start, end));
	needactivate = flags & RF_ACTIVE;
	/*
	 * Pass memory requests to nexus device
	 */
	passthrough = (device_get_parent(child) != bus);
	rle = NULL;

	dprintf("%s: entry (%p, %p, %d, %d, %p, %p, %jd, %d)\n",
	    __func__, bus, child, type, *rid, (void *)(intptr_t)start,
	    (void *)(intptr_t)end, count, flags);

	if (passthrough)
		return (BUS_ALLOC_RESOURCE(device_get_parent(bus), child, type,
		    rid, start, end, count, flags));

	/*
	 * If this is an allocation of the "default" range for a given RID,
	 * and we know what the resources for this device are (ie. they aren't
	 * maintained by a child bus), then work out the start/end values.
	 */

	if (isdefault) {
		rle = resource_list_find(&ivar->resources, type, *rid);
		if (rle == NULL) {
			return (NULL);
		}

		if (rle->res != NULL) {
			panic("%s: resource entry is busy", __func__);
		}
		start = rle->start;
		end = rle->end;
		count = rle->count;

		dprintf("%s: default resource (%p, %p, %jd)\n",
		    __func__, (void *)(intptr_t)start,
		    (void *)(intptr_t)end, count);
	}

	switch (type) {
	case SYS_RES_IRQ:
		rm = &sc->apb_irq_rman;
		break;
	case SYS_RES_MEMORY:
		rm = &sc->apb_mem_rman;
		break;
	default:
		printf("%s: unknown resource type %d\n", __func__, type);
		return (0);
	}

	rv = rman_reserve_resource(rm, start, end, count, flags, child);
	if (rv == NULL) {
		printf("%s: could not reserve resource %d\n", __func__, type);
		return (0);
	}

	rman_set_rid(rv, *rid);

	if (needactivate) {
		if (bus_activate_resource(child, type, *rid, rv)) {
			printf("%s: could not activate resource\n", __func__);
			rman_release_resource(rv);
			return (0);
		}
	}

	return (rv);
}

static int
apb_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{

	/* XXX: should we mask/unmask IRQ here? */
	return (BUS_ACTIVATE_RESOURCE(device_get_parent(bus), child,
		type, rid, r));
}

static int
apb_deactivate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{

	/* XXX: should we mask/unmask IRQ here? */
	return (BUS_DEACTIVATE_RESOURCE(device_get_parent(bus), child,
		type, rid, r));
}

static int
apb_release_resource(device_t dev, device_t child, int type,
    int rid, struct resource *r)
{
	struct resource_list *rl;
	struct resource_list_entry *rle;

	rl = apb_get_resource_list(dev, child);
	if (rl == NULL)
		return (EINVAL);
	rle = resource_list_find(rl, type, rid);
	if (rle == NULL)
		return (EINVAL);
	rman_release_resource(r);
	rle->res = NULL;

	return (0);
}


static int
apb_setup_intr(device_t bus, device_t child, struct resource *ires,
		int flags, driver_filter_t *filt, driver_intr_t *handler,
		void *arg, void **cookiep)
{
	struct apb_softc *sc = device_get_softc(bus);
	int error;
	int irq;
#ifndef INTRNG
	struct intr_event *event;
#endif

#ifdef INTRNG
	struct intr_irqsrc *isrc;
	const char *name;
	
	if ((rman_get_flags(ires) & RF_SHAREABLE) == 0)
		flags |= INTR_EXCL;

	irq = rman_get_start(ires);
	isrc = PIC_INTR_ISRC(sc, irq);
	if(isrc->isrc_event == 0) {
		error = intr_event_create(&isrc->isrc_event, (void *)irq,
		    0, irq, apb_mask_irq, apb_unmask_irq,
		    NULL, NULL, "apb intr%d:", irq);
		if(error != 0)
			return(error);
	}
	name = device_get_nameunit(child);
	error = intr_event_add_handler(isrc->isrc_event, name, filt, handler,
            arg, intr_priority(flags), flags, cookiep);
	return(error);
#else
	irq = rman_get_start(ires);

	if (irq > APB_IRQ_END)
		panic("%s: bad irq %d", __func__, irq);

	event = sc->sc_eventstab[irq];
	if (event == NULL) {
		error = intr_event_create(&event, (void *)irq, 0, irq, 
		    apb_mask_irq, apb_unmask_irq,
		    NULL, NULL,
		    "apb intr%d:", irq);

		if (error == 0) {
			sc->sc_eventstab[irq] = event;
			sc->sc_intr_counter[irq] =
			    mips_intrcnt_create(event->ie_name);
		}
		else
			return (error);
	}

	intr_event_add_handler(event, device_get_nameunit(child), filt,
	    handler, arg, intr_priority(flags), flags, cookiep);
	mips_intrcnt_setname(sc->sc_intr_counter[irq], event->ie_fullname);

	apb_unmask_irq((void*)irq);

	return (0);
#endif
}

#ifndef INTRNG
static int
apb_teardown_intr(device_t dev, device_t child, struct resource *ires,
    void *cookie)
{
#ifdef INTRNG
	 return (intr_teardown_irq(child, ires, cookie));
#else
	struct apb_softc *sc = device_get_softc(dev);
	int irq, result;

	irq = rman_get_start(ires);
	if (irq > APB_IRQ_END)
		panic("%s: bad irq %d", __func__, irq);

	if (sc->sc_eventstab[irq] == NULL)
		panic("Trying to teardown unoccupied IRQ");

	apb_mask_irq((void*)irq);

	result = intr_event_remove_handler(cookie);
	if (!result)
		sc->sc_eventstab[irq] = NULL;

	return (result);
#endif
}


static int
apb_filter(void *arg)
{
	struct apb_softc *sc = arg;
	struct intr_event *event;
	uint32_t reg, irq;

	if(ar531x_soc >= AR531X_SOC_AR5315)
		reg = ATH_READ_REG(AR5315_SYSREG_BASE +
			AR5315_SYSREG_MISC_INTSTAT);
	else
		reg = ATH_READ_REG(AR5312_SYSREG_BASE +
			AR5312_SYSREG_MISC_INTSTAT);

	for (irq = 0; irq < APB_NIRQS; irq++) {
		if (reg & (1 << irq)) {

			if(ar531x_soc >= AR531X_SOC_AR5315) {
				ATH_WRITE_REG(AR5315_SYSREG_BASE +
				    AR5315_SYSREG_MISC_INTSTAT,
				    reg & ~(1 << irq));
			} else {
				ATH_WRITE_REG(AR5312_SYSREG_BASE +
				    AR5312_SYSREG_MISC_INTSTAT,
				    reg & ~(1 << irq));
			}

			event = sc->sc_eventstab[irq];
			if (!event || CK_SLIST_EMPTY(&event->ie_handlers)) {
				if(irq == 1 && ar531x_soc < AR531X_SOC_AR5315) {
					ATH_READ_REG(AR5312_SYSREG_BASE +
						AR5312_SYSREG_AHBPERR);
					ATH_READ_REG(AR5312_SYSREG_BASE +
						AR5312_SYSREG_AHBDMAE);
				}
				/* Ignore non handle interrupts */
				if (irq != 0 && irq != 6)
					printf("Stray APB IRQ %d\n", irq);

				continue;
			}

			intr_event_handle(event, PCPU_GET(curthread)->td_intr_frame);
			mips_intrcnt_inc(sc->sc_intr_counter[irq]);
		}
	}

	return (FILTER_HANDLED);
}
#else
static int
apb_filter(void *arg)
{
	struct apb_softc *sc = arg;
	struct thread *td;
	uint32_t i, intr;

	td = curthread;
	/* Workaround: do not inflate intr nesting level */
	td->td_intr_nesting_level--;

	if(ar531x_soc >= AR531X_SOC_AR5315)
		intr = ATH_READ_REG(AR5315_SYSREG_BASE +
			AR5315_SYSREG_MISC_INTSTAT);
	else
		intr = ATH_READ_REG(AR5312_SYSREG_BASE +
			AR5312_SYSREG_MISC_INTSTAT);

	while ((i = fls(intr)) != 0) {
		i--;
		intr &= ~(1u << i);

		if(i == 1 && ar531x_soc < AR531X_SOC_AR5315) {
			ATH_READ_REG(AR5312_SYSREG_BASE +
			    AR5312_SYSREG_AHBPERR);
			ATH_READ_REG(AR5312_SYSREG_BASE +
			    AR5312_SYSREG_AHBDMAE);
		}

		if (intr_isrc_dispatch(PIC_INTR_ISRC(sc, i),
		    curthread->td_intr_frame) != 0) {
			device_printf(sc->apb_dev,
			    "Stray interrupt %u detected\n", i);
			apb_mask_irq((void*)i);
			continue;
		}
	}

	KASSERT(i == 0, ("all interrupts handled"));

	td->td_intr_nesting_level++;

	return (FILTER_HANDLED);

}

#endif

static void
apb_hinted_child(device_t bus, const char *dname, int dunit)
{
	device_t		child;
	long			maddr;
	int			msize;
	int			irq;
	int			result;
	int			mem_hints_count;

	child = BUS_ADD_CHILD(bus, 0, dname, dunit);

	/*
	 * Set hard-wired resources for hinted child using
	 * specific RIDs.
	 */
	mem_hints_count = 0;
	if (resource_long_value(dname, dunit, "maddr", &maddr) == 0)
		mem_hints_count++;
	if (resource_int_value(dname, dunit, "msize", &msize) == 0)
		mem_hints_count++;

	/* check if all info for mem resource has been provided */
	if ((mem_hints_count > 0) && (mem_hints_count < 2)) {
		printf("Either maddr or msize hint is missing for %s%d\n",
		    dname, dunit);
	} else if (mem_hints_count) {
		result = bus_set_resource(child, SYS_RES_MEMORY, 0,
		    maddr, msize);
		if (result != 0)
			device_printf(bus, 
			    "warning: bus_set_resource() failed\n");
	}

	if (resource_int_value(dname, dunit, "irq", &irq) == 0) {
		result = bus_set_resource(child, SYS_RES_IRQ, 0, irq, 1);
		if (result != 0)
			device_printf(bus,
			    "warning: bus_set_resource() failed\n");
	}
}

static device_t
apb_add_child(device_t bus, u_int order, const char *name, int unit)
{
	device_t		child;
	struct apb_ivar	*ivar;

	ivar = malloc(sizeof(struct apb_ivar), M_DEVBUF, M_WAITOK | M_ZERO);
	if (ivar == NULL) {
		printf("Failed to allocate ivar\n");
		return (0);
	}
	resource_list_init(&ivar->resources);

	child = device_add_child_ordered(bus, order, name, unit);
	if (child == NULL) {
		printf("Can't add child %s%d ordered\n", name, unit);
		return (0);
	}

	device_set_ivars(child, ivar);

	return (child);
}

/*
 * Helper routine for bus_generic_rl_get_resource/bus_generic_rl_set_resource
 * Provides pointer to resource_list for these routines
 */
static struct resource_list *
apb_get_resource_list(device_t dev, device_t child)
{
	struct apb_ivar *ivar;

	ivar = device_get_ivars(child);
	return (&(ivar->resources));
}

#ifdef INTRNG
static void
apb_pic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;

	irq = ((struct apb_pic_irqsrc *)isrc)->irq;
	apb_unmask_irq((void*)irq);
}

static void
apb_pic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;

	irq = ((struct apb_pic_irqsrc *)isrc)->irq;
	apb_mask_irq((void*)irq);
}

static void
apb_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	apb_pic_disable_intr(dev, isrc);
}

static void
apb_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	apb_pic_enable_intr(dev, isrc);
}

static void
apb_pic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	uint32_t reg, irq;

	irq = ((struct apb_pic_irqsrc *)isrc)->irq;
	if(ar531x_soc >= AR531X_SOC_AR5315) {
		reg = ATH_READ_REG(AR5315_SYSREG_BASE +
			AR5315_SYSREG_MISC_INTSTAT);
		ATH_WRITE_REG(AR5315_SYSREG_BASE + AR5315_SYSREG_MISC_INTSTAT,
		    reg & ~(1 << irq));
	} else {
		reg = ATH_READ_REG(AR5312_SYSREG_BASE +
			AR5312_SYSREG_MISC_INTSTAT);
		ATH_WRITE_REG(AR5312_SYSREG_BASE + AR5312_SYSREG_MISC_INTSTAT,
		    reg & ~(1 << irq));
	}
}

static int
apb_pic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	return (ENOTSUP);
}

#endif

static device_method_t apb_methods[] = {
	DEVMETHOD(bus_activate_resource,	apb_activate_resource),
	DEVMETHOD(bus_add_child,		apb_add_child),
	DEVMETHOD(bus_alloc_resource,		apb_alloc_resource),
	DEVMETHOD(bus_deactivate_resource,	apb_deactivate_resource),
	DEVMETHOD(bus_get_resource_list,	apb_get_resource_list),
	DEVMETHOD(bus_hinted_child,		apb_hinted_child),
	DEVMETHOD(bus_release_resource,		apb_release_resource),
	DEVMETHOD(device_attach,		apb_attach),
	DEVMETHOD(device_probe,			apb_probe),
	DEVMETHOD(bus_get_resource,		bus_generic_rl_get_resource),
	DEVMETHOD(bus_set_resource,		bus_generic_rl_set_resource),
#ifdef INTRNG
	DEVMETHOD(pic_disable_intr,		apb_pic_disable_intr),
	DEVMETHOD(pic_enable_intr,		apb_pic_enable_intr),
	DEVMETHOD(pic_map_intr,			apb_pic_map_intr),
	DEVMETHOD(pic_post_filter,		apb_pic_post_filter),
	DEVMETHOD(pic_post_ithread,		apb_pic_post_ithread),
	DEVMETHOD(pic_pre_ithread,		apb_pic_pre_ithread),

//	DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
#else
	DEVMETHOD(bus_teardown_intr,		apb_teardown_intr),
#endif
	DEVMETHOD(bus_setup_intr,		apb_setup_intr),

	DEVMETHOD_END
};

static driver_t apb_driver = {
	"apb",
	apb_methods,
	sizeof(struct apb_softc),
};
static devclass_t apb_devclass;

EARLY_DRIVER_MODULE(apb, nexus, apb_driver, apb_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
