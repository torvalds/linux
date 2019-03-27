/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Juli Mallett <jmallett@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

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
#include <sys/smp.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <mips/cavium/octeon_irq.h>

/*
 * This bus sits between devices/buses and nexus and handles CIU interrupts
 * and passes everything else through.  It should really be a nexus subclass
 * or something, but for now this will be sufficient.
 */

#define	CIU_IRQ_HARD		(0)

#define	CIU_IRQ_EN0_BEGIN	OCTEON_IRQ_WORKQ0
#define	CIU_IRQ_EN0_END		OCTEON_IRQ_BOOTDMA
#define	CIU_IRQ_EN0_COUNT	((CIU_IRQ_EN0_END - CIU_IRQ_EN0_BEGIN) + 1)

#define	CIU_IRQ_EN1_BEGIN	OCTEON_IRQ_WDOG0
#define	CIU_IRQ_EN1_END		OCTEON_IRQ_DFM
#define	CIU_IRQ_EN1_COUNT	((CIU_IRQ_EN1_END - CIU_IRQ_EN1_BEGIN) + 1)

struct ciu_softc {
	struct rman irq_rman;
	struct resource *ciu_irq;
};

static mips_intrcnt_t ciu_en0_intrcnt[CIU_IRQ_EN0_COUNT];
static mips_intrcnt_t ciu_en1_intrcnt[CIU_IRQ_EN1_COUNT];

static struct intr_event *ciu_en0_intr_events[CIU_IRQ_EN0_COUNT];
static struct intr_event *ciu_en1_intr_events[CIU_IRQ_EN1_COUNT];

static int		ciu_probe(device_t);
static int		ciu_attach(device_t);
static struct resource	*ciu_alloc_resource(device_t, device_t, int, int *,
					    rman_res_t, rman_res_t, rman_res_t,
					    u_int);
static int		ciu_setup_intr(device_t, device_t, struct resource *,
				       int, driver_filter_t *, driver_intr_t *,
				       void *, void **);
static int		ciu_teardown_intr(device_t, device_t,
					  struct resource *, void *);
static int		ciu_bind_intr(device_t, device_t, struct resource *,
				      int);
static int		ciu_describe_intr(device_t, device_t,
					  struct resource *, void *,
					  const char *);
static void		ciu_hinted_child(device_t, const char *, int);

static void		ciu_en0_intr_mask(void *);
static void		ciu_en0_intr_unmask(void *);
#ifdef SMP
static int		ciu_en0_intr_bind(void *, int);
#endif

static void		ciu_en1_intr_mask(void *);
static void		ciu_en1_intr_unmask(void *);
#ifdef SMP
static int		ciu_en1_intr_bind(void *, int);
#endif

static int		ciu_intr(void *);

static int
ciu_probe(device_t dev)
{
	if (device_get_unit(dev) != 0)
		return (ENXIO);

	device_set_desc(dev, "Cavium Octeon Central Interrupt Unit");
	return (BUS_PROBE_NOWILDCARD);
}

static int
ciu_attach(device_t dev)
{
	char name[MAXCOMLEN + 1];
	struct ciu_softc *sc;
	unsigned i;
	int error;
	int rid;

	sc = device_get_softc(dev);

	rid = 0;
	sc->ciu_irq = bus_alloc_resource(dev, SYS_RES_IRQ, &rid, CIU_IRQ_HARD,
					 CIU_IRQ_HARD, 1, RF_ACTIVE);
	if (sc->ciu_irq == NULL) {
		device_printf(dev, "could not allocate irq%d\n", CIU_IRQ_HARD);
		return (ENXIO);
	}

	error = bus_setup_intr(dev, sc->ciu_irq, INTR_TYPE_MISC, ciu_intr,
			       NULL, sc, NULL);
	if (error != 0) {
		device_printf(dev, "bus_setup_intr failed: %d\n", error);
		return (error);
	}

	sc->irq_rman.rm_type = RMAN_ARRAY;
	sc->irq_rman.rm_descr = "CIU IRQ";
	
	error = rman_init(&sc->irq_rman);
	if (error != 0)
		return (error);

	/*
	 * We have two contiguous IRQ regions, use a single rman.
	 */
	error = rman_manage_region(&sc->irq_rman, CIU_IRQ_EN0_BEGIN,
				   CIU_IRQ_EN1_END);
	if (error != 0)
		return (error);

	for (i = 0; i < CIU_IRQ_EN0_COUNT; i++) {
		snprintf(name, sizeof name, "int%d:", i + CIU_IRQ_EN0_BEGIN);
		ciu_en0_intrcnt[i] = mips_intrcnt_create(name);
	}

	for (i = 0; i < CIU_IRQ_EN1_COUNT; i++) {
		snprintf(name, sizeof name, "int%d:", i + CIU_IRQ_EN1_BEGIN);
		ciu_en1_intrcnt[i] = mips_intrcnt_create(name);
	}

	bus_generic_probe(dev);
	bus_generic_attach(dev);

	return (0);
}

static struct resource *
ciu_alloc_resource(device_t bus, device_t child, int type, int *rid,
		   rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource *res;
	struct ciu_softc *sc;
	
	sc = device_get_softc(bus);

	switch (type) {
	case SYS_RES_IRQ:
		break;
	default:
		return (bus_alloc_resource(device_get_parent(bus), type, rid,
					   start, end, count, flags));
	}

	/*
	 * One interrupt at a time for now.
	 */
	if (start != end)
		return (NULL);

	res = rman_reserve_resource(&sc->irq_rman, start, end, count, flags,
				    child);
	if (res != NULL)
		return (res);

	return (NULL);
}

static int
ciu_setup_intr(device_t bus, device_t child, struct resource *res, int flags,
	       driver_filter_t *filter, driver_intr_t *intr, void *arg,
	       void **cookiep)
{
	struct intr_event *event, **eventp;
	void (*mask_func)(void *);
	void (*unmask_func)(void *);
	int (*bind_func)(void *, int);
	mips_intrcnt_t intrcnt;
	int error;
	int irq;

	irq = rman_get_start(res);
	if (irq <= CIU_IRQ_EN0_END) {
		eventp = &ciu_en0_intr_events[irq - CIU_IRQ_EN0_BEGIN];
		intrcnt = ciu_en0_intrcnt[irq - CIU_IRQ_EN0_BEGIN];
		mask_func = ciu_en0_intr_mask;
		unmask_func = ciu_en0_intr_unmask;
#ifdef SMP
		bind_func = ciu_en0_intr_bind;
#endif
	} else {
		eventp = &ciu_en1_intr_events[irq - CIU_IRQ_EN1_BEGIN];
		intrcnt = ciu_en1_intrcnt[irq - CIU_IRQ_EN1_BEGIN];
		mask_func = ciu_en1_intr_mask;
		unmask_func = ciu_en1_intr_unmask;
#ifdef SMP
		bind_func = ciu_en1_intr_bind;
#endif
	}
#if !defined(SMP)
	bind_func = NULL;
#endif

	if ((event = *eventp) == NULL) {
		error = intr_event_create(eventp, (void *)(uintptr_t)irq, 0,
		    irq, mask_func, unmask_func, NULL, bind_func, "int%d", irq);
		if (error != 0)
			return (error);

		event = *eventp;

		unmask_func((void *)(uintptr_t)irq);
	}

	intr_event_add_handler(event, device_get_nameunit(child),
	    filter, intr, arg, intr_priority(flags), flags, cookiep);

	mips_intrcnt_setname(intrcnt, event->ie_fullname);

	return (0);
}

static int
ciu_teardown_intr(device_t bus, device_t child, struct resource *res,
		  void *cookie)
{
	int error;

	error = intr_event_remove_handler(cookie);
	if (error != 0)
		return (error);

	return (0);
}

#ifdef SMP
static int
ciu_bind_intr(device_t bus, device_t child, struct resource *res, int cpu)
{
	struct intr_event *event;
	int irq;
	
	irq = rman_get_start(res);
	if (irq <= CIU_IRQ_EN0_END)
		event = ciu_en0_intr_events[irq - CIU_IRQ_EN0_BEGIN];
	else
		event = ciu_en1_intr_events[irq - CIU_IRQ_EN1_BEGIN];

	return (intr_event_bind(event, cpu));
}
#endif

static int
ciu_describe_intr(device_t bus, device_t child, struct resource *res,
		  void *cookie, const char *descr)
{
	struct intr_event *event;
	mips_intrcnt_t intrcnt;
	int error;
	int irq;
	
	irq = rman_get_start(res);
	if (irq <= CIU_IRQ_EN0_END) {
		event = ciu_en0_intr_events[irq - CIU_IRQ_EN0_BEGIN];
		intrcnt = ciu_en0_intrcnt[irq - CIU_IRQ_EN0_BEGIN];
	} else {
		event = ciu_en1_intr_events[irq - CIU_IRQ_EN1_BEGIN];
		intrcnt = ciu_en1_intrcnt[irq - CIU_IRQ_EN1_BEGIN];
	}

	error = intr_event_describe_handler(event, cookie, descr);
	if (error != 0)
		return (error);

	mips_intrcnt_setname(intrcnt, event->ie_fullname);

	return (0);
}

static void
ciu_hinted_child(device_t bus, const char *dname, int dunit)
{
	BUS_ADD_CHILD(bus, 0, dname, dunit);
}

static void
ciu_en0_intr_mask(void *arg)
{
	uint64_t mask;
	int irq;

	irq = (uintptr_t)arg;
	mask = cvmx_read_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num()*2));
	mask &= ~(1ull << (irq - CIU_IRQ_EN0_BEGIN));
	cvmx_write_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num()*2), mask);
}

static void
ciu_en0_intr_unmask(void *arg)
{
	uint64_t mask;
	int irq;

	irq = (uintptr_t)arg;
	mask = cvmx_read_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num()*2));
	mask |= 1ull << (irq - CIU_IRQ_EN0_BEGIN);
	cvmx_write_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num()*2), mask);
}

#ifdef SMP
static int
ciu_en0_intr_bind(void *arg, int target)
{
	uint64_t mask;
	int core;
	int irq;

	irq = (uintptr_t)arg;
	CPU_FOREACH(core) {
		mask = cvmx_read_csr(CVMX_CIU_INTX_EN0(core*2));
		if (core == target)
			mask |= 1ull << (irq - CIU_IRQ_EN0_BEGIN);
		else
			mask &= ~(1ull << (irq - CIU_IRQ_EN0_BEGIN));
		cvmx_write_csr(CVMX_CIU_INTX_EN0(core*2), mask);
	}

	return (0);
}
#endif

static void
ciu_en1_intr_mask(void *arg)
{
	uint64_t mask;
	int irq;

	irq = (uintptr_t)arg;
	mask = cvmx_read_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num()*2));
	mask &= ~(1ull << (irq - CIU_IRQ_EN1_BEGIN));
	cvmx_write_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num()*2), mask);
}

static void
ciu_en1_intr_unmask(void *arg)
{
	uint64_t mask;
	int irq;

	irq = (uintptr_t)arg;
	mask = cvmx_read_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num()*2));
	mask |= 1ull << (irq - CIU_IRQ_EN1_BEGIN);
	cvmx_write_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num()*2), mask);
}

#ifdef SMP
static int
ciu_en1_intr_bind(void *arg, int target)
{
	uint64_t mask;
	int core;
	int irq;

	irq = (uintptr_t)arg;
	CPU_FOREACH(core) {
		mask = cvmx_read_csr(CVMX_CIU_INTX_EN1(core*2));
		if (core == target)
			mask |= 1ull << (irq - CIU_IRQ_EN1_BEGIN);
		else
			mask &= ~(1ull << (irq - CIU_IRQ_EN1_BEGIN));
		cvmx_write_csr(CVMX_CIU_INTX_EN1(core*2), mask);
	}

	return (0);
}
#endif

static int
ciu_intr(void *arg)
{
	struct ciu_softc *sc;
	uint64_t en0_sum, en1_sum;
	uint64_t en0_mask, en1_mask;
	int irq_index;
	int error;

	sc = arg;
	(void)sc;

	en0_sum = cvmx_read_csr(CVMX_CIU_INTX_SUM0(cvmx_get_core_num()*2));
	en1_sum = cvmx_read_csr(CVMX_CIU_INT_SUM1);

	en0_mask = cvmx_read_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num()*2));
	en1_mask = cvmx_read_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num()*2));

	en0_sum &= en0_mask;
	en1_sum &= en1_mask;

	if (en0_sum == 0 && en1_sum == 0)
		return (FILTER_STRAY);

	for (irq_index = 0; en0_sum != 0; irq_index++, en0_sum >>= 1) {
		if ((en0_sum & 1) == 0)
			continue;

		mips_intrcnt_inc(ciu_en0_intrcnt[irq_index]);

		error = intr_event_handle(ciu_en0_intr_events[irq_index], NULL);
		if (error != 0)
			printf("%s: stray en0 irq%d\n", __func__, irq_index);
	}

	for (irq_index = 0; en1_sum != 0; irq_index++, en1_sum >>= 1) {
		if ((en1_sum & 1) == 0)
			continue;

		mips_intrcnt_inc(ciu_en1_intrcnt[irq_index]);

		error = intr_event_handle(ciu_en1_intr_events[irq_index], NULL);
		if (error != 0)
			printf("%s: stray en1 irq%d\n", __func__, irq_index);
	}

	return (FILTER_HANDLED);
}

static device_method_t ciu_methods[] = {
	DEVMETHOD(device_probe,		ciu_probe),
	DEVMETHOD(device_attach,	ciu_attach),

	DEVMETHOD(bus_alloc_resource,	ciu_alloc_resource),
	DEVMETHOD(bus_activate_resource,bus_generic_activate_resource),
	DEVMETHOD(bus_setup_intr,	ciu_setup_intr),
	DEVMETHOD(bus_teardown_intr,	ciu_teardown_intr),
#ifdef SMP
	DEVMETHOD(bus_bind_intr,	ciu_bind_intr),
#endif
	DEVMETHOD(bus_describe_intr,	ciu_describe_intr),

	DEVMETHOD(bus_add_child,	bus_generic_add_child),
	DEVMETHOD(bus_hinted_child,	ciu_hinted_child),

	{ 0, 0 }
};

static driver_t ciu_driver = {
	"ciu",
	ciu_methods,
	sizeof(struct ciu_softc),
};
static devclass_t ciu_devclass;
DRIVER_MODULE(ciu, nexus, ciu_driver, ciu_devclass, 0, 0);
