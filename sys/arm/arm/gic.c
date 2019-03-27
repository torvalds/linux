/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * Developed by Damjan Marion <damjan.marion@gmail.com>
 *
 * Based on OMAP4 GIC code by Ben Gray
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include "opt_acpi.h"
#include "opt_platform.h"

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
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/sched.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/smp.h>

#ifdef FDT
#include <dev/fdt/fdt_intr.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>
#endif

#include <arm/arm/gic.h>
#include <arm/arm/gic_common.h>

#include "pic_if.h"
#include "msi_if.h"

/* We are using GICv2 register naming */

/* Distributor Registers */

/* CPU Registers */
#define GICC_CTLR		0x0000			/* v1 ICCICR */
#define GICC_PMR		0x0004			/* v1 ICCPMR */
#define GICC_BPR		0x0008			/* v1 ICCBPR */
#define GICC_IAR		0x000C			/* v1 ICCIAR */
#define GICC_EOIR		0x0010			/* v1 ICCEOIR */
#define GICC_RPR		0x0014			/* v1 ICCRPR */
#define GICC_HPPIR		0x0018			/* v1 ICCHPIR */
#define GICC_ABPR		0x001C			/* v1 ICCABPR */
#define GICC_IIDR		0x00FC			/* v1 ICCIIDR*/

/* TYPER Registers */
#define	GICD_TYPER_SECURITYEXT	0x400
#define	GIC_SUPPORT_SECEXT(_sc)	\
    ((_sc->typer & GICD_TYPER_SECURITYEXT) == GICD_TYPER_SECURITYEXT)


#ifndef	GIC_DEFAULT_ICFGR_INIT
#define	GIC_DEFAULT_ICFGR_INIT	0x00000000
#endif

struct gic_irqsrc {
	struct intr_irqsrc	gi_isrc;
	uint32_t		gi_irq;
	enum intr_polarity	gi_pol;
	enum intr_trigger	gi_trig;
#define GI_FLAG_EARLY_EOI	(1 << 0)
#define GI_FLAG_MSI		(1 << 1) /* This interrupt source should only */
					 /* be used for MSI/MSI-X interrupts */
#define GI_FLAG_MSI_USED	(1 << 2) /* This irq is already allocated */
					 /* for a MSI/MSI-X interrupt */
	u_int			gi_flags;
};

static u_int gic_irq_cpu;
static int arm_gic_bind_intr(device_t dev, struct intr_irqsrc *isrc);

#ifdef SMP
static u_int sgi_to_ipi[GIC_LAST_SGI - GIC_FIRST_SGI + 1];
static u_int sgi_first_unused = GIC_FIRST_SGI;
#endif

#define GIC_INTR_ISRC(sc, irq)	(&sc->gic_irqs[irq].gi_isrc)

static struct resource_spec arm_gic_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },	/* Distributor registers */
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },	/* CPU Interrupt Intf. registers */
	{ SYS_RES_IRQ,	  0, RF_ACTIVE | RF_OPTIONAL }, /* Parent interrupt */
	{ -1, 0 }
};


#if defined(__arm__) && defined(INVARIANTS)
static int gic_debug_spurious = 1;
#else
static int gic_debug_spurious = 0;
#endif
TUNABLE_INT("hw.gic.debug_spurious", &gic_debug_spurious);

static u_int arm_gic_map[MAXCPU];

static struct arm_gic_softc *gic_sc = NULL;

#define	gic_c_read_4(_sc, _reg)		\
    bus_space_read_4((_sc)->gic_c_bst, (_sc)->gic_c_bsh, (_reg))
#define	gic_c_write_4(_sc, _reg, _val)		\
    bus_space_write_4((_sc)->gic_c_bst, (_sc)->gic_c_bsh, (_reg), (_val))
#define	gic_d_read_4(_sc, _reg)		\
    bus_space_read_4((_sc)->gic_d_bst, (_sc)->gic_d_bsh, (_reg))
#define	gic_d_write_1(_sc, _reg, _val)		\
    bus_space_write_1((_sc)->gic_d_bst, (_sc)->gic_d_bsh, (_reg), (_val))
#define	gic_d_write_4(_sc, _reg, _val)		\
    bus_space_write_4((_sc)->gic_d_bst, (_sc)->gic_d_bsh, (_reg), (_val))

static inline void
gic_irq_unmask(struct arm_gic_softc *sc, u_int irq)
{

	gic_d_write_4(sc, GICD_ISENABLER(irq), GICD_I_MASK(irq));
}

static inline void
gic_irq_mask(struct arm_gic_softc *sc, u_int irq)
{

	gic_d_write_4(sc, GICD_ICENABLER(irq), GICD_I_MASK(irq));
}

static uint8_t
gic_cpu_mask(struct arm_gic_softc *sc)
{
	uint32_t mask;
	int i;

	/* Read the current cpuid mask by reading ITARGETSR{0..7} */
	for (i = 0; i < 8; i++) {
		mask = gic_d_read_4(sc, GICD_ITARGETSR(4 * i));
		if (mask != 0)
			break;
	}
	/* No mask found, assume we are on CPU interface 0 */
	if (mask == 0)
		return (1);

	/* Collect the mask in the lower byte */
	mask |= mask >> 16;
	mask |= mask >> 8;

	return (mask);
}

#ifdef SMP
static void
arm_gic_init_secondary(device_t dev)
{
	struct arm_gic_softc *sc = device_get_softc(dev);
	u_int irq, cpu;

	/* Set the mask so we can find this CPU to send it IPIs */
	cpu = PCPU_GET(cpuid);
	arm_gic_map[cpu] = gic_cpu_mask(sc);

	for (irq = 0; irq < sc->nirqs; irq += 4)
		gic_d_write_4(sc, GICD_IPRIORITYR(irq), 0);

	/* Set all the interrupts to be in Group 0 (secure) */
	for (irq = 0; GIC_SUPPORT_SECEXT(sc) && irq < sc->nirqs; irq += 32) {
		gic_d_write_4(sc, GICD_IGROUPR(irq), 0);
	}

	/* Enable CPU interface */
	gic_c_write_4(sc, GICC_CTLR, 1);

	/* Set priority mask register. */
	gic_c_write_4(sc, GICC_PMR, 0xff);

	/* Enable interrupt distribution */
	gic_d_write_4(sc, GICD_CTLR, 0x01);

	/* Unmask attached SGI interrupts. */
	for (irq = GIC_FIRST_SGI; irq <= GIC_LAST_SGI; irq++)
		if (intr_isrc_init_on_cpu(GIC_INTR_ISRC(sc, irq), cpu))
			gic_irq_unmask(sc, irq);

	/* Unmask attached PPI interrupts. */
	for (irq = GIC_FIRST_PPI; irq <= GIC_LAST_PPI; irq++)
		if (intr_isrc_init_on_cpu(GIC_INTR_ISRC(sc, irq), cpu))
			gic_irq_unmask(sc, irq);
}
#endif /* SMP */

static int
arm_gic_register_isrcs(struct arm_gic_softc *sc, uint32_t num)
{
	int error;
	uint32_t irq;
	struct gic_irqsrc *irqs;
	struct intr_irqsrc *isrc;
	const char *name;

	irqs = malloc(num * sizeof(struct gic_irqsrc), M_DEVBUF,
	    M_WAITOK | M_ZERO);

	name = device_get_nameunit(sc->gic_dev);
	for (irq = 0; irq < num; irq++) {
		irqs[irq].gi_irq = irq;
		irqs[irq].gi_pol = INTR_POLARITY_CONFORM;
		irqs[irq].gi_trig = INTR_TRIGGER_CONFORM;

		isrc = &irqs[irq].gi_isrc;
		if (irq <= GIC_LAST_SGI) {
			error = intr_isrc_register(isrc, sc->gic_dev,
			    INTR_ISRCF_IPI, "%s,i%u", name, irq - GIC_FIRST_SGI);
		} else if (irq <= GIC_LAST_PPI) {
			error = intr_isrc_register(isrc, sc->gic_dev,
			    INTR_ISRCF_PPI, "%s,p%u", name, irq - GIC_FIRST_PPI);
		} else {
			error = intr_isrc_register(isrc, sc->gic_dev, 0,
			    "%s,s%u", name, irq - GIC_FIRST_SPI);
		}
		if (error != 0) {
			/* XXX call intr_isrc_deregister() */
			free(irqs, M_DEVBUF);
			return (error);
		}
	}
	sc->gic_irqs = irqs;
	sc->nirqs = num;
	return (0);
}

static void
arm_gic_reserve_msi_range(device_t dev, u_int start, u_int count)
{
	struct arm_gic_softc *sc;
	int i;

	sc = device_get_softc(dev);

	KASSERT((start + count) < sc->nirqs,
	    ("%s: Trying to allocate too many MSI IRQs: %d + %d > %d", __func__,
	    start, count, sc->nirqs));
	for (i = 0; i < count; i++) {
		KASSERT(sc->gic_irqs[start + i].gi_isrc.isrc_handlers == 0,
		    ("%s: MSI interrupt %d already has a handler", __func__,
		    count + i));
		KASSERT(sc->gic_irqs[start + i].gi_pol == INTR_POLARITY_CONFORM,
		    ("%s: MSI interrupt %d already has a polarity", __func__,
		    count + i));
		KASSERT(sc->gic_irqs[start + i].gi_trig == INTR_TRIGGER_CONFORM,
		    ("%s: MSI interrupt %d already has a trigger", __func__,
		    count + i));
		sc->gic_irqs[start + i].gi_pol = INTR_POLARITY_HIGH;
		sc->gic_irqs[start + i].gi_trig = INTR_TRIGGER_EDGE;
		sc->gic_irqs[start + i].gi_flags |= GI_FLAG_MSI;
	}
}

int
arm_gic_attach(device_t dev)
{
	struct		arm_gic_softc *sc;
	int		i;
	uint32_t	icciidr, mask, nirqs;

	if (gic_sc)
		return (ENXIO);

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, arm_gic_spec, sc->gic_res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->gic_dev = dev;
	gic_sc = sc;

	/* Initialize mutex */
	mtx_init(&sc->mutex, "GIC lock", NULL, MTX_SPIN);

	/* Distributor Interface */
	sc->gic_d_bst = rman_get_bustag(sc->gic_res[0]);
	sc->gic_d_bsh = rman_get_bushandle(sc->gic_res[0]);

	/* CPU Interface */
	sc->gic_c_bst = rman_get_bustag(sc->gic_res[1]);
	sc->gic_c_bsh = rman_get_bushandle(sc->gic_res[1]);

	/* Disable interrupt forwarding to the CPU interface */
	gic_d_write_4(sc, GICD_CTLR, 0x00);

	/* Get the number of interrupts */
	sc->typer = gic_d_read_4(sc, GICD_TYPER);
	nirqs = GICD_TYPER_I_NUM(sc->typer);

	if (arm_gic_register_isrcs(sc, nirqs)) {
		device_printf(dev, "could not register irqs\n");
		goto cleanup;
	}

	icciidr = gic_c_read_4(sc, GICC_IIDR);
	device_printf(dev,
	    "pn 0x%x, arch 0x%x, rev 0x%x, implementer 0x%x irqs %u\n",
	    GICD_IIDR_PROD(icciidr), GICD_IIDR_VAR(icciidr),
	    GICD_IIDR_REV(icciidr), GICD_IIDR_IMPL(icciidr), sc->nirqs);
	sc->gic_iidr = icciidr;

	/* Set all global interrupts to be level triggered, active low. */
	for (i = 32; i < sc->nirqs; i += 16) {
		gic_d_write_4(sc, GICD_ICFGR(i), GIC_DEFAULT_ICFGR_INIT);
	}

	/* Disable all interrupts. */
	for (i = 32; i < sc->nirqs; i += 32) {
		gic_d_write_4(sc, GICD_ICENABLER(i), 0xFFFFFFFF);
	}

	/* Find the current cpu mask */
	mask = gic_cpu_mask(sc);
	/* Set the mask so we can find this CPU to send it IPIs */
	arm_gic_map[PCPU_GET(cpuid)] = mask;
	/* Set all four targets to this cpu */
	mask |= mask << 8;
	mask |= mask << 16;

	for (i = 0; i < sc->nirqs; i += 4) {
		gic_d_write_4(sc, GICD_IPRIORITYR(i), 0);
		if (i > 32) {
			gic_d_write_4(sc, GICD_ITARGETSR(i), mask);
		}
	}

	/* Set all the interrupts to be in Group 0 (secure) */
	for (i = 0; GIC_SUPPORT_SECEXT(sc) && i < sc->nirqs; i += 32) {
		gic_d_write_4(sc, GICD_IGROUPR(i), 0);
	}

	/* Enable CPU interface */
	gic_c_write_4(sc, GICC_CTLR, 1);

	/* Set priority mask register. */
	gic_c_write_4(sc, GICC_PMR, 0xff);

	/* Enable interrupt distribution */
	gic_d_write_4(sc, GICD_CTLR, 0x01);
	return (0);

cleanup:
	arm_gic_detach(dev);
	return(ENXIO);
}

int
arm_gic_detach(device_t dev)
{
	struct arm_gic_softc *sc;

	sc = device_get_softc(dev);

	if (sc->gic_irqs != NULL)
		free(sc->gic_irqs, M_DEVBUF);

	bus_release_resources(dev, arm_gic_spec, sc->gic_res);

	return (0);
}

static int
arm_gic_print_child(device_t bus, device_t child)
{
	struct resource_list *rl;
	int rv;

	rv = bus_print_child_header(bus, child);

	rl = BUS_GET_RESOURCE_LIST(bus, child);
	if (rl != NULL) {
		rv += resource_list_print_type(rl, "mem", SYS_RES_MEMORY,
		    "%#jx");
		rv += resource_list_print_type(rl, "irq", SYS_RES_IRQ, "%jd");
	}

	rv += bus_print_child_footer(bus, child);

	return (rv);
}

static struct resource *
arm_gic_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct arm_gic_softc *sc;
	struct resource_list_entry *rle;
	struct resource_list *rl;
	int j;

	KASSERT(type == SYS_RES_MEMORY, ("Invalid resoure type %x", type));

	sc = device_get_softc(bus);

	/*
	 * Request for the default allocation with a given rid: use resource
	 * list stored in the local device info.
	 */
	if (RMAN_IS_DEFAULT_RANGE(start, end)) {
		rl = BUS_GET_RESOURCE_LIST(bus, child);

		if (type == SYS_RES_IOPORT)
			type = SYS_RES_MEMORY;

		rle = resource_list_find(rl, type, *rid);
		if (rle == NULL) {
			if (bootverbose)
				device_printf(bus, "no default resources for "
				    "rid = %d, type = %d\n", *rid, type);
			return (NULL);
		}
		start = rle->start;
		end = rle->end;
		count = rle->count;
	}

	/* Remap through ranges property */
	for (j = 0; j < sc->nranges; j++) {
		if (start >= sc->ranges[j].bus && end <
		    sc->ranges[j].bus + sc->ranges[j].size) {
			start -= sc->ranges[j].bus;
			start += sc->ranges[j].host;
			end -= sc->ranges[j].bus;
			end += sc->ranges[j].host;
			break;
		}
	}
	if (j == sc->nranges && sc->nranges != 0) {
		if (bootverbose)
			device_printf(bus, "Could not map resource "
			    "%#jx-%#jx\n", (uintmax_t)start, (uintmax_t)end);

		return (NULL);
	}

	return (bus_generic_alloc_resource(bus, child, type, rid, start, end,
	    count, flags));
}

static int
arm_gic_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct arm_gic_softc *sc;

	sc = device_get_softc(dev);

	switch(which) {
	case GIC_IVAR_HW_REV:
		KASSERT(GICD_IIDR_VAR(sc->gic_iidr) < 3,
		    ("arm_gic_read_ivar: Unknown IIDR revision %u (%.08x)",
		     GICD_IIDR_VAR(sc->gic_iidr), sc->gic_iidr));
		*result = GICD_IIDR_VAR(sc->gic_iidr);
		return (0);
	case GIC_IVAR_BUS:
		KASSERT(sc->gic_bus != GIC_BUS_UNKNOWN,
		    ("arm_gic_read_ivar: Unknown bus type"));
		KASSERT(sc->gic_bus <= GIC_BUS_MAX,
		    ("arm_gic_read_ivar: Invalid bus type %u", sc->gic_bus));
		*result = sc->gic_bus;
		return (0);
	}

	return (ENOENT);
}

int
arm_gic_intr(void *arg)
{
	struct arm_gic_softc *sc = arg;
	struct gic_irqsrc *gi;
	uint32_t irq_active_reg, irq;
	struct trapframe *tf;

	irq_active_reg = gic_c_read_4(sc, GICC_IAR);
	irq = irq_active_reg & 0x3FF;

	/*
	 * 1. We do EOI here because recent read value from active interrupt
	 *    register must be used for it. Another approach is to save this
	 *    value into associated interrupt source.
	 * 2. EOI must be done on same CPU where interrupt has fired. Thus
	 *    we must ensure that interrupted thread does not migrate to
	 *    another CPU.
	 * 3. EOI cannot be delayed by any preemption which could happen on
	 *    critical_exit() used in MI intr code, when interrupt thread is
	 *    scheduled. See next point.
	 * 4. IPI_RENDEZVOUS assumes that no preemption is permitted during
	 *    an action and any use of critical_exit() could break this
	 *    assumption. See comments within smp_rendezvous_action().
	 * 5. We always return FILTER_HANDLED as this is an interrupt
	 *    controller dispatch function. Otherwise, in cascaded interrupt
	 *    case, the whole interrupt subtree would be masked.
	 */

	if (irq >= sc->nirqs) {
		if (gic_debug_spurious)
			device_printf(sc->gic_dev,
			    "Spurious interrupt detected: last irq: %d on CPU%d\n",
			    sc->last_irq[PCPU_GET(cpuid)], PCPU_GET(cpuid));
		return (FILTER_HANDLED);
	}

	tf = curthread->td_intr_frame;
dispatch_irq:
	gi = sc->gic_irqs + irq;
	/*
	 * Note that GIC_FIRST_SGI is zero and is not used in 'if' statement
	 * as compiler complains that comparing u_int >= 0 is always true.
	 */
	if (irq <= GIC_LAST_SGI) {
#ifdef SMP
		/* Call EOI for all IPI before dispatch. */
		gic_c_write_4(sc, GICC_EOIR, irq_active_reg);
		intr_ipi_dispatch(sgi_to_ipi[gi->gi_irq], tf);
		goto next_irq;
#else
		device_printf(sc->gic_dev, "SGI %u on UP system detected\n",
		    irq - GIC_FIRST_SGI);
		gic_c_write_4(sc, GICC_EOIR, irq_active_reg);
		goto next_irq;
#endif
	}

	if (gic_debug_spurious)
		sc->last_irq[PCPU_GET(cpuid)] = irq;
	if ((gi->gi_flags & GI_FLAG_EARLY_EOI) == GI_FLAG_EARLY_EOI)
		gic_c_write_4(sc, GICC_EOIR, irq_active_reg);

	if (intr_isrc_dispatch(&gi->gi_isrc, tf) != 0) {
		gic_irq_mask(sc, irq);
		if ((gi->gi_flags & GI_FLAG_EARLY_EOI) != GI_FLAG_EARLY_EOI)
			gic_c_write_4(sc, GICC_EOIR, irq_active_reg);
		device_printf(sc->gic_dev, "Stray irq %u disabled\n", irq);
	}

next_irq:
	arm_irq_memory_barrier(irq);
	irq_active_reg = gic_c_read_4(sc, GICC_IAR);
	irq = irq_active_reg & 0x3FF;
	if (irq < sc->nirqs)
		goto dispatch_irq;

	return (FILTER_HANDLED);
}

static void
gic_config(struct arm_gic_softc *sc, u_int irq, enum intr_trigger trig,
    enum intr_polarity pol)
{
	uint32_t reg;
	uint32_t mask;

	if (irq < GIC_FIRST_SPI)
		return;

	mtx_lock_spin(&sc->mutex);

	reg = gic_d_read_4(sc, GICD_ICFGR(irq));
	mask = (reg >> 2*(irq % 16)) & 0x3;

	if (pol == INTR_POLARITY_LOW) {
		mask &= ~GICD_ICFGR_POL_MASK;
		mask |= GICD_ICFGR_POL_LOW;
	} else if (pol == INTR_POLARITY_HIGH) {
		mask &= ~GICD_ICFGR_POL_MASK;
		mask |= GICD_ICFGR_POL_HIGH;
	}

	if (trig == INTR_TRIGGER_LEVEL) {
		mask &= ~GICD_ICFGR_TRIG_MASK;
		mask |= GICD_ICFGR_TRIG_LVL;
	} else if (trig == INTR_TRIGGER_EDGE) {
		mask &= ~GICD_ICFGR_TRIG_MASK;
		mask |= GICD_ICFGR_TRIG_EDGE;
	}

	/* Set mask */
	reg = reg & ~(0x3 << 2*(irq % 16));
	reg = reg | (mask << 2*(irq % 16));
	gic_d_write_4(sc, GICD_ICFGR(irq), reg);

	mtx_unlock_spin(&sc->mutex);
}

static int
gic_bind(struct arm_gic_softc *sc, u_int irq, cpuset_t *cpus)
{
	uint32_t cpu, end, mask;

	end = min(mp_ncpus, 8);
	for (cpu = end; cpu < MAXCPU; cpu++)
		if (CPU_ISSET(cpu, cpus))
			return (EINVAL);

	for (mask = 0, cpu = 0; cpu < end; cpu++)
		if (CPU_ISSET(cpu, cpus))
			mask |= arm_gic_map[cpu];

	gic_d_write_1(sc, GICD_ITARGETSR(0) + irq, mask);
	return (0);
}

#ifdef FDT
static int
gic_map_fdt(device_t dev, u_int ncells, pcell_t *cells, u_int *irqp,
    enum intr_polarity *polp, enum intr_trigger *trigp)
{

	if (ncells == 1) {
		*irqp = cells[0];
		*polp = INTR_POLARITY_CONFORM;
		*trigp = INTR_TRIGGER_CONFORM;
		return (0);
	}
	if (ncells == 3) {
		u_int irq, tripol;

		/*
		 * The 1st cell is the interrupt type:
		 *	0 = SPI
		 *	1 = PPI
		 * The 2nd cell contains the interrupt number:
		 *	[0 - 987] for SPI
		 *	[0 -  15] for PPI
		 * The 3rd cell is the flags, encoded as follows:
		 *   bits[3:0] trigger type and level flags
		 *	1 = low-to-high edge triggered
		 *	2 = high-to-low edge triggered
		 *	4 = active high level-sensitive
		 *	8 = active low level-sensitive
		 *   bits[15:8] PPI interrupt cpu mask
		 *	Each bit corresponds to each of the 8 possible cpus
		 *	attached to the GIC.  A bit set to '1' indicated
		 *	the interrupt is wired to that CPU.
		 */
		switch (cells[0]) {
		case 0:
			irq = GIC_FIRST_SPI + cells[1];
			/* SPI irq is checked later. */
			break;
		case 1:
			irq = GIC_FIRST_PPI + cells[1];
			if (irq > GIC_LAST_PPI) {
				device_printf(dev, "unsupported PPI interrupt "
				    "number %u\n", cells[1]);
				return (EINVAL);
			}
			break;
		default:
			device_printf(dev, "unsupported interrupt type "
			    "configuration %u\n", cells[0]);
			return (EINVAL);
		}

		tripol = cells[2] & 0xff;
		if (tripol & 0xf0 || (tripol & FDT_INTR_LOW_MASK &&
		    cells[0] == 0))
			device_printf(dev, "unsupported trigger/polarity "
			    "configuration 0x%02x\n", tripol);

		*irqp = irq;
		*polp = INTR_POLARITY_CONFORM;
		*trigp = tripol & FDT_INTR_EDGE_MASK ?
		    INTR_TRIGGER_EDGE : INTR_TRIGGER_LEVEL;
		return (0);
	}
	return (EINVAL);
}
#endif

static int
gic_map_msi(device_t dev, struct intr_map_data_msi *msi_data, u_int *irqp,
    enum intr_polarity *polp, enum intr_trigger *trigp)
{
	struct gic_irqsrc *gi;

	/* Map a non-GICv2m MSI */
	gi = (struct gic_irqsrc *)msi_data->isrc;
	if (gi == NULL)
		return (ENXIO);

	*irqp = gi->gi_irq;

	/* MSI/MSI-X interrupts are always edge triggered with high polarity */
	*polp = INTR_POLARITY_HIGH;
	*trigp = INTR_TRIGGER_EDGE;

	return (0);
}

static int
gic_map_intr(device_t dev, struct intr_map_data *data, u_int *irqp,
    enum intr_polarity *polp, enum intr_trigger *trigp)
{
	u_int irq;
	enum intr_polarity pol;
	enum intr_trigger trig;
	struct arm_gic_softc *sc;
	struct intr_map_data_msi *dam;
#ifdef FDT
	struct intr_map_data_fdt *daf;
#endif
#ifdef DEV_ACPI
	struct intr_map_data_acpi *daa;
#endif

	sc = device_get_softc(dev);
	switch (data->type) {
#ifdef FDT
	case INTR_MAP_DATA_FDT:
		daf = (struct intr_map_data_fdt *)data;
		if (gic_map_fdt(dev, daf->ncells, daf->cells, &irq, &pol,
		    &trig) != 0)
			return (EINVAL);
		KASSERT(irq >= sc->nirqs ||
		    (sc->gic_irqs[irq].gi_flags & GI_FLAG_MSI) == 0,
		    ("%s: Attempting to map a MSI interrupt from FDT",
		    __func__));
		break;
#endif
#ifdef DEV_ACPI
	case INTR_MAP_DATA_ACPI:
		daa = (struct intr_map_data_acpi *)data;
		irq = daa->irq;
		pol = daa->pol;
		trig = daa->trig;
		break;
#endif
	case INTR_MAP_DATA_MSI:
		/* Non-GICv2m MSI */
		dam = (struct intr_map_data_msi *)data;
		if (gic_map_msi(dev, dam, &irq, &pol, &trig) != 0)
			return (EINVAL);
		break;
	default:
		return (ENOTSUP);
	}

	if (irq >= sc->nirqs)
		return (EINVAL);
	if (pol != INTR_POLARITY_CONFORM && pol != INTR_POLARITY_LOW &&
	    pol != INTR_POLARITY_HIGH)
		return (EINVAL);
	if (trig != INTR_TRIGGER_CONFORM && trig != INTR_TRIGGER_EDGE &&
	    trig != INTR_TRIGGER_LEVEL)
		return (EINVAL);

	*irqp = irq;
	if (polp != NULL)
		*polp = pol;
	if (trigp != NULL)
		*trigp = trig;
	return (0);
}

static int
arm_gic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	int error;
	u_int irq;
	struct arm_gic_softc *sc;

	error = gic_map_intr(dev, data, &irq, NULL, NULL);
	if (error == 0) {
		sc = device_get_softc(dev);
		*isrcp = GIC_INTR_ISRC(sc, irq);
	}
	return (error);
}

static int
arm_gic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct arm_gic_softc *sc = device_get_softc(dev);
	struct gic_irqsrc *gi = (struct gic_irqsrc *)isrc;
	enum intr_trigger trig;
	enum intr_polarity pol;

	if ((gi->gi_flags & GI_FLAG_MSI) == GI_FLAG_MSI) {
		/* GICv2m MSI */
		pol = gi->gi_pol;
		trig = gi->gi_trig;
		KASSERT(pol == INTR_POLARITY_HIGH,
		    ("%s: MSI interrupts must be active-high", __func__));
		KASSERT(trig == INTR_TRIGGER_EDGE,
		    ("%s: MSI interrupts must be edge triggered", __func__));
	} else if (data != NULL) {
		u_int irq;

		/* Get config for resource. */
		if (gic_map_intr(dev, data, &irq, &pol, &trig) ||
		    gi->gi_irq != irq)
			return (EINVAL);
	} else {
		pol = INTR_POLARITY_CONFORM;
		trig = INTR_TRIGGER_CONFORM;
	}

	/* Compare config if this is not first setup. */
	if (isrc->isrc_handlers != 0) {
		if ((pol != INTR_POLARITY_CONFORM && pol != gi->gi_pol) ||
		    (trig != INTR_TRIGGER_CONFORM && trig != gi->gi_trig))
			return (EINVAL);
		else
			return (0);
	}

	/* For MSI/MSI-X we should have already configured these */
	if ((gi->gi_flags & GI_FLAG_MSI) == 0) {
		if (pol == INTR_POLARITY_CONFORM)
			pol = INTR_POLARITY_LOW;	/* just pick some */
		if (trig == INTR_TRIGGER_CONFORM)
			trig = INTR_TRIGGER_EDGE;	/* just pick some */

		gi->gi_pol = pol;
		gi->gi_trig = trig;

		/* Edge triggered interrupts need an early EOI sent */
		if (gi->gi_trig == INTR_TRIGGER_EDGE)
			gi->gi_flags |= GI_FLAG_EARLY_EOI;
	}

	/*
	 * XXX - In case that per CPU interrupt is going to be enabled in time
	 *       when SMP is already started, we need some IPI call which
	 *       enables it on others CPUs. Further, it's more complicated as
	 *       pic_enable_source() and pic_disable_source() should act on
	 *       per CPU basis only. Thus, it should be solved here somehow.
	 */
	if (isrc->isrc_flags & INTR_ISRCF_PPI)
		CPU_SET(PCPU_GET(cpuid), &isrc->isrc_cpu);

	gic_config(sc, gi->gi_irq, gi->gi_trig, gi->gi_pol);
	arm_gic_bind_intr(dev, isrc);
	return (0);
}

static int
arm_gic_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct gic_irqsrc *gi = (struct gic_irqsrc *)isrc;

	if (isrc->isrc_handlers == 0 && (gi->gi_flags & GI_FLAG_MSI) == 0) {
		gi->gi_pol = INTR_POLARITY_CONFORM;
		gi->gi_trig = INTR_TRIGGER_CONFORM;
	}
	return (0);
}

static void
arm_gic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct arm_gic_softc *sc = device_get_softc(dev);
	struct gic_irqsrc *gi = (struct gic_irqsrc *)isrc;

	arm_irq_memory_barrier(gi->gi_irq);
	gic_irq_unmask(sc, gi->gi_irq);
}

static void
arm_gic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct arm_gic_softc *sc = device_get_softc(dev);
	struct gic_irqsrc *gi = (struct gic_irqsrc *)isrc;

	gic_irq_mask(sc, gi->gi_irq);
}

static void
arm_gic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct arm_gic_softc *sc = device_get_softc(dev);
	struct gic_irqsrc *gi = (struct gic_irqsrc *)isrc;

	arm_gic_disable_intr(dev, isrc);
	gic_c_write_4(sc, GICC_EOIR, gi->gi_irq);
}

static void
arm_gic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	arm_irq_memory_barrier(0);
	arm_gic_enable_intr(dev, isrc);
}

static void
arm_gic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct arm_gic_softc *sc = device_get_softc(dev);
	struct gic_irqsrc *gi = (struct gic_irqsrc *)isrc;

        /* EOI for edge-triggered done earlier. */
	if ((gi->gi_flags & GI_FLAG_EARLY_EOI) == GI_FLAG_EARLY_EOI)
		return;

	arm_irq_memory_barrier(0);
	gic_c_write_4(sc, GICC_EOIR, gi->gi_irq);
}

static int
arm_gic_bind_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct arm_gic_softc *sc = device_get_softc(dev);
	struct gic_irqsrc *gi = (struct gic_irqsrc *)isrc;

	if (gi->gi_irq < GIC_FIRST_SPI)
		return (EINVAL);

	if (CPU_EMPTY(&isrc->isrc_cpu)) {
		gic_irq_cpu = intr_irq_next_cpu(gic_irq_cpu, &all_cpus);
		CPU_SETOF(gic_irq_cpu, &isrc->isrc_cpu);
	}
	return (gic_bind(sc, gi->gi_irq, &isrc->isrc_cpu));
}

#ifdef SMP
static void
arm_gic_ipi_send(device_t dev, struct intr_irqsrc *isrc, cpuset_t cpus,
    u_int ipi)
{
	struct arm_gic_softc *sc = device_get_softc(dev);
	struct gic_irqsrc *gi = (struct gic_irqsrc *)isrc;
	uint32_t val = 0, i;

	for (i = 0; i < MAXCPU; i++)
		if (CPU_ISSET(i, &cpus))
			val |= arm_gic_map[i] << GICD_SGI_TARGET_SHIFT;

	gic_d_write_4(sc, GICD_SGIR, val | gi->gi_irq);
}

static int
arm_gic_ipi_setup(device_t dev, u_int ipi, struct intr_irqsrc **isrcp)
{
	struct intr_irqsrc *isrc;
	struct arm_gic_softc *sc = device_get_softc(dev);

	if (sgi_first_unused > GIC_LAST_SGI)
		return (ENOSPC);

	isrc = GIC_INTR_ISRC(sc, sgi_first_unused);
	sgi_to_ipi[sgi_first_unused++] = ipi;

	CPU_SET(PCPU_GET(cpuid), &isrc->isrc_cpu);

	*isrcp = isrc;
	return (0);
}
#endif

static device_method_t arm_gic_methods[] = {
	/* Bus interface */
	DEVMETHOD(bus_print_child,	arm_gic_print_child),
	DEVMETHOD(bus_add_child,	bus_generic_add_child),
	DEVMETHOD(bus_alloc_resource,	arm_gic_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource,bus_generic_activate_resource),
	DEVMETHOD(bus_read_ivar,	arm_gic_read_ivar),

	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	arm_gic_disable_intr),
	DEVMETHOD(pic_enable_intr,	arm_gic_enable_intr),
	DEVMETHOD(pic_map_intr,		arm_gic_map_intr),
	DEVMETHOD(pic_setup_intr,	arm_gic_setup_intr),
	DEVMETHOD(pic_teardown_intr,	arm_gic_teardown_intr),
	DEVMETHOD(pic_post_filter,	arm_gic_post_filter),
	DEVMETHOD(pic_post_ithread,	arm_gic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	arm_gic_pre_ithread),
#ifdef SMP
	DEVMETHOD(pic_bind_intr,	arm_gic_bind_intr),
	DEVMETHOD(pic_init_secondary,	arm_gic_init_secondary),
	DEVMETHOD(pic_ipi_send,		arm_gic_ipi_send),
	DEVMETHOD(pic_ipi_setup,	arm_gic_ipi_setup),
#endif
	{ 0, 0 }
};

DEFINE_CLASS_0(gic, arm_gic_driver, arm_gic_methods,
    sizeof(struct arm_gic_softc));

/*
 * GICv2m support -- the GICv2 MSI/MSI-X controller.
 */

#define	GICV2M_MSI_TYPER	0x008
#define	 MSI_TYPER_SPI_BASE(x)	(((x) >> 16) & 0x3ff)
#define	 MSI_TYPER_SPI_COUNT(x)	(((x) >> 0) & 0x3ff)
#define	GICv2M_MSI_SETSPI_NS	0x040
#define	GICV2M_MSI_IIDR		0xFCC

int
arm_gicv2m_attach(device_t dev)
{
	struct arm_gicv2m_softc *sc;
	uint32_t typer;
	int rid;

	sc = device_get_softc(dev);

	rid = 0;
	sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->sc_mem == NULL) {
		device_printf(dev, "Unable to allocate resources\n");
		return (ENXIO);
	}

	typer = bus_read_4(sc->sc_mem, GICV2M_MSI_TYPER);
	sc->sc_spi_start = MSI_TYPER_SPI_BASE(typer);
	sc->sc_spi_count = MSI_TYPER_SPI_COUNT(typer);
	sc->sc_spi_end = sc->sc_spi_start + sc->sc_spi_count;

	/* Reserve these interrupts for MSI/MSI-X use */
	arm_gic_reserve_msi_range(device_get_parent(dev), sc->sc_spi_start,
	    sc->sc_spi_count);

	mtx_init(&sc->sc_mutex, "GICv2m lock", NULL, MTX_DEF);

	intr_msi_register(dev, sc->sc_xref);

	if (bootverbose)
		device_printf(dev, "using spi %u to %u\n", sc->sc_spi_start,
		    sc->sc_spi_start + sc->sc_spi_count - 1);

	return (0);
}

static int
arm_gicv2m_alloc_msi(device_t dev, device_t child, int count, int maxcount,
    device_t *pic, struct intr_irqsrc **srcs)
{
	struct arm_gic_softc *psc;
	struct arm_gicv2m_softc *sc;
	int i, irq, end_irq;
	bool found;

	KASSERT(powerof2(count), ("%s: bad count", __func__));
	KASSERT(powerof2(maxcount), ("%s: bad maxcount", __func__));

	psc = device_get_softc(device_get_parent(dev));
	sc = device_get_softc(dev);

	mtx_lock(&sc->sc_mutex);

	found = false;
	for (irq = sc->sc_spi_start; irq < sc->sc_spi_end; irq++) {
		/* Start on an aligned interrupt */
		if ((irq & (maxcount - 1)) != 0)
			continue;

		/* Assume we found a valid range until shown otherwise */
		found = true;

		/* Check this range is valid */
		for (end_irq = irq; end_irq != irq + count; end_irq++) {
			/* No free interrupts */
			if (end_irq == sc->sc_spi_end) {
				found = false;
				break;
			}

			KASSERT((psc->gic_irqs[end_irq].gi_flags & GI_FLAG_MSI)!= 0,
			    ("%s: Non-MSI interrupt found", __func__));

			/* This is already used */
			if ((psc->gic_irqs[end_irq].gi_flags & GI_FLAG_MSI_USED) ==
			    GI_FLAG_MSI_USED) {
				found = false;
				break;
			}
		}
		if (found)
			break;
	}

	/* Not enough interrupts were found */
	if (!found || irq == sc->sc_spi_end) {
		mtx_unlock(&sc->sc_mutex);
		return (ENXIO);
	}

	for (i = 0; i < count; i++) {
		/* Mark the interrupt as used */
		psc->gic_irqs[irq + i].gi_flags |= GI_FLAG_MSI_USED;

	}
	mtx_unlock(&sc->sc_mutex);

	for (i = 0; i < count; i++)
		srcs[i] = (struct intr_irqsrc *)&psc->gic_irqs[irq + i];
	*pic = device_get_parent(dev);

	return (0);
}

static int
arm_gicv2m_release_msi(device_t dev, device_t child, int count,
    struct intr_irqsrc **isrc)
{
	struct arm_gicv2m_softc *sc;
	struct gic_irqsrc *gi;
	int i;

	sc = device_get_softc(dev);

	mtx_lock(&sc->sc_mutex);
	for (i = 0; i < count; i++) {
		gi = (struct gic_irqsrc *)isrc[i];

		KASSERT((gi->gi_flags & GI_FLAG_MSI_USED) == GI_FLAG_MSI_USED,
		    ("%s: Trying to release an unused MSI-X interrupt",
		    __func__));

		gi->gi_flags &= ~GI_FLAG_MSI_USED;
	}
	mtx_unlock(&sc->sc_mutex);

	return (0);
}

static int
arm_gicv2m_alloc_msix(device_t dev, device_t child, device_t *pic,
    struct intr_irqsrc **isrcp)
{
	struct arm_gicv2m_softc *sc;
	struct arm_gic_softc *psc;
	int irq;

	psc = device_get_softc(device_get_parent(dev));
	sc = device_get_softc(dev);

	mtx_lock(&sc->sc_mutex);
	/* Find an unused interrupt */
	for (irq = sc->sc_spi_start; irq < sc->sc_spi_end; irq++) {
		KASSERT((psc->gic_irqs[irq].gi_flags & GI_FLAG_MSI) != 0,
		    ("%s: Non-MSI interrupt found", __func__));
		if ((psc->gic_irqs[irq].gi_flags & GI_FLAG_MSI_USED) == 0)
			break;
	}
	/* No free interrupt was found */
	if (irq == sc->sc_spi_end) {
		mtx_unlock(&sc->sc_mutex);
		return (ENXIO);
	}

	/* Mark the interrupt as used */
	psc->gic_irqs[irq].gi_flags |= GI_FLAG_MSI_USED;
	mtx_unlock(&sc->sc_mutex);

	*isrcp = (struct intr_irqsrc *)&psc->gic_irqs[irq];
	*pic = device_get_parent(dev);

	return (0);
}

static int
arm_gicv2m_release_msix(device_t dev, device_t child, struct intr_irqsrc *isrc)
{
	struct arm_gicv2m_softc *sc;
	struct gic_irqsrc *gi;

	sc = device_get_softc(dev);
	gi = (struct gic_irqsrc *)isrc;

	KASSERT((gi->gi_flags & GI_FLAG_MSI_USED) == GI_FLAG_MSI_USED,
	    ("%s: Trying to release an unused MSI-X interrupt", __func__));

	mtx_lock(&sc->sc_mutex);
	gi->gi_flags &= ~GI_FLAG_MSI_USED;
	mtx_unlock(&sc->sc_mutex);

	return (0);
}

static int
arm_gicv2m_map_msi(device_t dev, device_t child, struct intr_irqsrc *isrc,
    uint64_t *addr, uint32_t *data)
{
	struct arm_gicv2m_softc *sc = device_get_softc(dev);
	struct gic_irqsrc *gi = (struct gic_irqsrc *)isrc;

	*addr = vtophys(rman_get_virtual(sc->sc_mem)) + GICv2M_MSI_SETSPI_NS;
	*data = gi->gi_irq;

	return (0);
}

static device_method_t arm_gicv2m_methods[] = {
	/* Device interface */
	DEVMETHOD(device_attach,	arm_gicv2m_attach),

	/* MSI/MSI-X */
	DEVMETHOD(msi_alloc_msi,	arm_gicv2m_alloc_msi),
	DEVMETHOD(msi_release_msi,	arm_gicv2m_release_msi),
	DEVMETHOD(msi_alloc_msix,	arm_gicv2m_alloc_msix),
	DEVMETHOD(msi_release_msix,	arm_gicv2m_release_msix),
	DEVMETHOD(msi_map_msi,		arm_gicv2m_map_msi),

	/* End */
	DEVMETHOD_END
};

DEFINE_CLASS_0(gicv2m, arm_gicv2m_driver, arm_gicv2m_methods,
    sizeof(struct arm_gicv2m_softc));
