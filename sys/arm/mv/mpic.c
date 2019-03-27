/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Benno Rice.
 * Copyright (C) 2007-2011 MARVELL INTERNATIONAL LTD.
 * Copyright (c) 2012 Semihalf.
 * All rights reserved.
 *
 * Developed by Semihalf.
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
 *
 * from: FreeBSD: //depot/projects/arm/src/sys/arm/xscale/pxa2x0/pxa2x0_icu.c, rev 1
 * from: FreeBSD: src/sys/arm/mv/ic.c,v 1.5 2011/02/08 01:49:30
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/cpuset.h>
#include <sys/ktr.h>
#include <sys/kdb.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/proc.h>
#include <sys/smp.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/smp.h>

#include <arm/mv/mvvar.h>
#include <arm/mv/mvreg.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/fdt/fdt_common.h>

#ifdef INTRNG
#include "pic_if.h"
#endif

#ifdef DEBUG
#define debugf(fmt, args...) do { printf("%s(): ", __func__);	\
    printf(fmt,##args); } while (0)
#else
#define debugf(fmt, args...)
#endif

#define	MPIC_INT_LOCAL			3
#define	MPIC_INT_ERR			4
#define	MPIC_INT_MSI			96

#define	MPIC_IRQ_MASK		0x3ff

#define	MPIC_CTRL		0x0
#define	MPIC_SOFT_INT		0x4
#define	MPIC_SOFT_INT_DRBL1	(1 << 5)
#define	MPIC_ERR_CAUSE		0x20
#define	MPIC_ISE		0x30
#define	MPIC_ICE		0x34
#define	MPIC_INT_CTL(irq)	(0x100 + (irq)*4)

#define	MPIC_INT_IRQ_FIQ_MASK(cpuid)	(0x101 << (cpuid))
#define	MPIC_CTRL_NIRQS(ctrl)	(((ctrl) >> 2) & 0x3ff)

#define	MPIC_IN_DRBL		0x08
#define	MPIC_IN_DRBL_MASK	0x0c
#define	MPIC_PPI_CAUSE		0x10
#define	MPIC_CTP		0x40
#define	MPIC_IIACK		0x44
#define	MPIC_ISM		0x48
#define	MPIC_ICM		0x4c
#define	MPIC_ERR_MASK		0x50
#define	MPIC_LOCAL_MASK		0x54
#define	MPIC_CPU(n)		(n) * 0x100

#define	MPIC_PPI	32

struct mv_mpic_irqsrc {
	struct intr_irqsrc	mmi_isrc;
	u_int			mmi_irq;
};

struct mv_mpic_softc {
	device_t		sc_dev;
	struct resource	*	mpic_res[4];
	bus_space_tag_t		mpic_bst;
	bus_space_handle_t	mpic_bsh;
	bus_space_tag_t		cpu_bst;
	bus_space_handle_t	cpu_bsh;
	bus_space_tag_t		drbl_bst;
	bus_space_handle_t	drbl_bsh;
	struct mtx		mtx;
	struct mv_mpic_irqsrc *	mpic_isrcs;
	int			nirqs;
	void *			intr_hand;
};

static struct resource_spec mv_mpic_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	2,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE | RF_OPTIONAL },
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{"mrvl,mpic",		true},
	{"marvell,mpic",	true},
	{NULL,			false}
};

static struct mv_mpic_softc *mv_mpic_sc = NULL;

void mpic_send_ipi(int cpus, u_int ipi);

static int	mv_mpic_probe(device_t);
static int	mv_mpic_attach(device_t);
uint32_t	mv_mpic_get_cause(void);
uint32_t	mv_mpic_get_cause_err(void);
uint32_t	mv_mpic_get_msi(void);
static void	mpic_unmask_irq(uintptr_t nb);
static void	mpic_mask_irq(uintptr_t nb);
static void	mpic_mask_irq_err(uintptr_t nb);
static void	mpic_unmask_irq_err(uintptr_t nb);
static boolean_t mpic_irq_is_percpu(uintptr_t);
static int	mpic_intr(void *arg);
static void	mpic_unmask_msi(void);
void mpic_init_secondary(device_t);
void mpic_ipi_send(device_t, struct intr_irqsrc*, cpuset_t, u_int);
int mpic_ipi_read(int);
void mpic_ipi_clear(int);

#define	MPIC_WRITE(softc, reg, val) \
    bus_space_write_4((softc)->mpic_bst, (softc)->mpic_bsh, (reg), (val))
#define	MPIC_READ(softc, reg) \
    bus_space_read_4((softc)->mpic_bst, (softc)->mpic_bsh, (reg))

#define MPIC_CPU_WRITE(softc, reg, val) \
    bus_space_write_4((softc)->cpu_bst, (softc)->cpu_bsh, (reg), (val))
#define MPIC_CPU_READ(softc, reg) \
    bus_space_read_4((softc)->cpu_bst, (softc)->cpu_bsh, (reg))

#define MPIC_DRBL_WRITE(softc, reg, val) \
    bus_space_write_4((softc)->drbl_bst, (softc)->drbl_bsh, (reg), (val))
#define MPIC_DRBL_READ(softc, reg) \
    bus_space_read_4((softc)->drbl_bst, (softc)->drbl_bsh, (reg))

static int
mv_mpic_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Marvell Integrated Interrupt Controller");
	return (0);
}

static int
mv_mpic_register_isrcs(struct mv_mpic_softc *sc)
{
	int error;
	uint32_t irq;
	struct intr_irqsrc *isrc;
	const char *name;

	sc->mpic_isrcs = malloc(sc->nirqs * sizeof (*sc->mpic_isrcs), M_DEVBUF,
	    M_WAITOK | M_ZERO);

	name = device_get_nameunit(sc->sc_dev);
	for (irq = 0; irq < sc->nirqs; irq++) {
		sc->mpic_isrcs[irq].mmi_irq = irq;

		isrc = &sc->mpic_isrcs[irq].mmi_isrc;
		if (irq < MPIC_PPI) {
			error = intr_isrc_register(isrc, sc->sc_dev,
			    INTR_ISRCF_PPI, "%s", name);
		} else {
			error = intr_isrc_register(isrc, sc->sc_dev, 0, "%s",
			    name);
		}
		if (error != 0) {
			/* XXX call intr_isrc_deregister() */
			device_printf(sc->sc_dev, "%s failed", __func__);
			return (error);
		}
	}
	return (0);
}

static int
mv_mpic_attach(device_t dev)
{
	struct mv_mpic_softc *sc;
	int error;
	uint32_t val;
	int cpu;

	sc = (struct mv_mpic_softc *)device_get_softc(dev);

	if (mv_mpic_sc != NULL)
		return (ENXIO);
	mv_mpic_sc = sc;

	sc->sc_dev = dev;

	mtx_init(&sc->mtx, "MPIC lock", NULL, MTX_SPIN);

	error = bus_alloc_resources(dev, mv_mpic_spec, sc->mpic_res);
	if (error) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}
	if (sc->mpic_res[3] == NULL)
		device_printf(dev, "No interrupt to use.\n");
	else
		bus_setup_intr(dev, sc->mpic_res[3], INTR_TYPE_CLK,
		    mpic_intr, NULL, sc, &sc->intr_hand);

	sc->mpic_bst = rman_get_bustag(sc->mpic_res[0]);
	sc->mpic_bsh = rman_get_bushandle(sc->mpic_res[0]);

	sc->cpu_bst = rman_get_bustag(sc->mpic_res[1]);
	sc->cpu_bsh = rman_get_bushandle(sc->mpic_res[1]);

	if (sc->mpic_res[2] != NULL) {
		/* This is required only if MSIs are used. */
		sc->drbl_bst = rman_get_bustag(sc->mpic_res[2]);
		sc->drbl_bsh = rman_get_bushandle(sc->mpic_res[2]);
	}

	MPIC_WRITE(mv_mpic_sc, MPIC_CTRL, 1);
	MPIC_CPU_WRITE(mv_mpic_sc, MPIC_CTP, 0);

	val = MPIC_READ(mv_mpic_sc, MPIC_CTRL);
	sc->nirqs = MPIC_CTRL_NIRQS(val);

	if (mv_mpic_register_isrcs(sc) != 0) {
		device_printf(dev, "could not register PIC ISRCs\n");
		bus_release_resources(dev, mv_mpic_spec, sc->mpic_res);
		return (ENXIO);
	}

	OF_device_register_xref(OF_xref_from_node(ofw_bus_get_node(dev)), dev);

	if (intr_pic_register(dev, OF_xref_from_device(dev)) == NULL) {
		device_printf(dev, "could not register PIC\n");
		bus_release_resources(dev, mv_mpic_spec, sc->mpic_res);
		return (ENXIO);
	}

	mpic_unmask_msi();

	/* Unmask CPU performance counters overflow irq */
	for (cpu = 0; cpu < mp_ncpus; cpu++)
		MPIC_CPU_WRITE(mv_mpic_sc, MPIC_CPU(cpu) + MPIC_LOCAL_MASK,
		    (1 << cpu) | MPIC_CPU_READ(mv_mpic_sc,
		    MPIC_CPU(cpu) + MPIC_LOCAL_MASK));

	return (0);
}

static int
mpic_intr(void *arg)
{
	struct mv_mpic_softc *sc;
	uint32_t cause, irqsrc;
	unsigned int irq;
	u_int cpuid;

	sc = arg;
	cpuid = PCPU_GET(cpuid);
	irq = 0;

	for (cause = MPIC_CPU_READ(sc, MPIC_PPI_CAUSE); cause > 0;
	    cause >>= 1, irq++) {
		if (cause & 1) {
			irqsrc = MPIC_READ(sc, MPIC_INT_CTL(irq));
			if ((irqsrc & MPIC_INT_IRQ_FIQ_MASK(cpuid)) == 0)
				continue;
			if (intr_isrc_dispatch(&sc->mpic_isrcs[irq].mmi_isrc,
			    curthread->td_intr_frame) != 0) {
				mpic_mask_irq(irq);
				device_printf(sc->sc_dev, "Stray irq %u "
				    "disabled\n", irq);
			}
		}
	}

	return (FILTER_HANDLED);
}

static void
mpic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;

	irq = ((struct mv_mpic_irqsrc *)isrc)->mmi_irq;
	mpic_mask_irq(irq);
}

static void
mpic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;

	irq = ((struct mv_mpic_irqsrc *)isrc)->mmi_irq;
	mpic_unmask_irq(irq);
}

static int
mpic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct intr_map_data_fdt *daf;
	struct mv_mpic_softc *sc;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	sc = device_get_softc(dev);
	daf = (struct intr_map_data_fdt *)data;

	if (daf->ncells !=1 || daf->cells[0] >= sc->nirqs)
		return (EINVAL);

	*isrcp = &sc->mpic_isrcs[daf->cells[0]].mmi_isrc;
	return (0);
}

static void
mpic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	mpic_disable_intr(dev, isrc);
}

static void
mpic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	mpic_enable_intr(dev, isrc);
}

static void
mpic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
}

static device_method_t mv_mpic_methods[] = {
	DEVMETHOD(device_probe,		mv_mpic_probe),
	DEVMETHOD(device_attach,	mv_mpic_attach),

	DEVMETHOD(pic_disable_intr,	mpic_disable_intr),
	DEVMETHOD(pic_enable_intr,	mpic_enable_intr),
	DEVMETHOD(pic_map_intr,		mpic_map_intr),
	DEVMETHOD(pic_post_filter,	mpic_post_filter),
	DEVMETHOD(pic_post_ithread,	mpic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	mpic_pre_ithread),
	DEVMETHOD(pic_init_secondary,	mpic_init_secondary),
	DEVMETHOD(pic_ipi_send,		mpic_ipi_send),
	{ 0, 0 }
};

static driver_t mv_mpic_driver = {
	"mpic",
	mv_mpic_methods,
	sizeof(struct mv_mpic_softc),
};

static devclass_t mv_mpic_devclass;

EARLY_DRIVER_MODULE(mpic, simplebus, mv_mpic_driver, mv_mpic_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);

static void
mpic_unmask_msi(void)
{

	mpic_unmask_irq(MPIC_INT_MSI);
}

static void
mpic_unmask_irq_err(uintptr_t nb)
{
	uint32_t mask;
	uint8_t bit_off;

	MPIC_WRITE(mv_mpic_sc, MPIC_ISE, MPIC_INT_ERR);
	MPIC_CPU_WRITE(mv_mpic_sc, MPIC_ICM, MPIC_INT_ERR);

	bit_off = nb - ERR_IRQ;
	mask = MPIC_CPU_READ(mv_mpic_sc, MPIC_ERR_MASK);
	mask |= (1 << bit_off);
	MPIC_CPU_WRITE(mv_mpic_sc, MPIC_ERR_MASK, mask);
}

static void
mpic_mask_irq_err(uintptr_t nb)
{
	uint32_t mask;
	uint8_t bit_off;

	bit_off = nb - ERR_IRQ;
	mask = MPIC_CPU_READ(mv_mpic_sc, MPIC_ERR_MASK);
	mask &= ~(1 << bit_off);
	MPIC_CPU_WRITE(mv_mpic_sc, MPIC_ERR_MASK, mask);
}

static boolean_t
mpic_irq_is_percpu(uintptr_t nb)
{
	if (nb < MPIC_PPI)
		return TRUE;

	return FALSE;
}

static void
mpic_unmask_irq(uintptr_t nb)
{

#ifdef SMP
	int cpu;

	if (nb == MPIC_INT_LOCAL) {
		for (cpu = 0; cpu < mp_ncpus; cpu++)
			MPIC_CPU_WRITE(mv_mpic_sc,
			    MPIC_CPU(cpu) + MPIC_ICM, nb);
		return;
	}
#endif
	if (mpic_irq_is_percpu(nb))
		MPIC_CPU_WRITE(mv_mpic_sc, MPIC_ICM, nb);
	else if (nb < ERR_IRQ)
		MPIC_WRITE(mv_mpic_sc, MPIC_ISE, nb);
	else if (nb < MSI_IRQ)
		mpic_unmask_irq_err(nb);

	if (nb == 0)
		MPIC_CPU_WRITE(mv_mpic_sc, MPIC_IN_DRBL_MASK, 0xffffffff);
}

static void
mpic_mask_irq(uintptr_t nb)
{

#ifdef SMP
	int cpu;

	if (nb == MPIC_INT_LOCAL) {
		for (cpu = 0; cpu < mp_ncpus; cpu++)
			MPIC_CPU_WRITE(mv_mpic_sc,
			    MPIC_CPU(cpu) + MPIC_ISM, nb);
		return;
	}
#endif
	if (mpic_irq_is_percpu(nb))
		MPIC_CPU_WRITE(mv_mpic_sc, MPIC_ISM, nb);
	else if (nb < ERR_IRQ)
		MPIC_WRITE(mv_mpic_sc, MPIC_ICE, nb);
	else if (nb < MSI_IRQ)
		mpic_mask_irq_err(nb);
}

uint32_t
mv_mpic_get_cause(void)
{

	return (MPIC_CPU_READ(mv_mpic_sc, MPIC_IIACK));
}

uint32_t
mv_mpic_get_cause_err(void)
{
	uint32_t err_cause;
	uint8_t bit_off;

	err_cause = MPIC_READ(mv_mpic_sc, MPIC_ERR_CAUSE);

	if (err_cause)
		bit_off = ffs(err_cause) - 1;
	else
		return (-1);

	debugf("%s: irq:%x cause:%x\n", __func__, bit_off, err_cause);
	return (ERR_IRQ + bit_off);
}

uint32_t
mv_mpic_get_msi(void)
{
	uint32_t cause;
	uint8_t bit_off;

	KASSERT(mv_mpic_sc->drbl_bst != NULL, ("No doorbell in mv_mpic_get_msi"));
	cause = MPIC_DRBL_READ(mv_mpic_sc, 0);

	if (cause)
		bit_off = ffs(cause) - 1;
	else
		return (-1);

	debugf("%s: irq:%x cause:%x\n", __func__, bit_off, cause);

	cause &= ~(1 << bit_off);
	MPIC_DRBL_WRITE(mv_mpic_sc, 0, cause);

	return (MSI_IRQ + bit_off);
}

int
mv_msi_data(int irq, uint64_t *addr, uint32_t *data)
{
	u_long phys, base, size;
	phandle_t node;
	int error;

	node = ofw_bus_get_node(mv_mpic_sc->sc_dev);

	/* Get physical address of register space */
	error = fdt_get_range(OF_parent(node), 0, &phys, &size);
	if (error) {
		printf("%s: Cannot get register physical address, err:%d",
		    __func__, error);
		return (error);
	}

	/* Get offset of MPIC register space */
	error = fdt_regsize(node, &base, &size);
	if (error) {
		printf("%s: Cannot get MPIC register offset, err:%d",
		    __func__, error);
		return (error);
	}

	*addr = phys + base + MPIC_SOFT_INT;
	*data = MPIC_SOFT_INT_DRBL1 | irq;

	return (0);
}

void
mpic_init_secondary(device_t dev)
{
}

void
mpic_ipi_send(device_t dev, struct intr_irqsrc *isrc, cpuset_t cpus, u_int ipi)
{
	uint32_t val, i;

	val = 0x00000000;
	for (i = 0; i < MAXCPU; i++)
		if (CPU_ISSET(i, &cpus))
			val |= (1 << (8 + i));
	val |= ipi;
	MPIC_WRITE(mv_mpic_sc, MPIC_SOFT_INT, val);
}

int
mpic_ipi_read(int i __unused)
{
	uint32_t val;
	int ipi;

	val = MPIC_CPU_READ(mv_mpic_sc, MPIC_IN_DRBL);
	if (val) {
		ipi = ffs(val) - 1;
		MPIC_CPU_WRITE(mv_mpic_sc, MPIC_IN_DRBL, ~(1 << ipi));
		return (ipi);
	}

	return (0x3ff);
}

void
mpic_ipi_clear(int ipi)
{
}
