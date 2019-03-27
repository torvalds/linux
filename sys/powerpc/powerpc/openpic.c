/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2002 Benno Rice.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/sched.h>
#include <sys/smp.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/pio.h>
#include <machine/resource.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/openpicreg.h>
#include <machine/openpicvar.h>

#include "pic_if.h"

devclass_t openpic_devclass;

/*
 * Local routines
 */
static int openpic_intr(void *arg);

static __inline uint32_t
openpic_read(struct openpic_softc *sc, u_int reg)
{
	return (bus_space_read_4(sc->sc_bt, sc->sc_bh, reg));
}

static __inline void
openpic_write(struct openpic_softc *sc, u_int reg, uint32_t val)
{
	bus_space_write_4(sc->sc_bt, sc->sc_bh, reg, val);
}

static __inline void
openpic_set_priority(struct openpic_softc *sc, int pri)
{
	u_int tpr;
	uint32_t x;

	sched_pin();
	tpr = OPENPIC_PCPU_TPR((sc->sc_dev == root_pic) ? PCPU_GET(cpuid) : 0);
	x = openpic_read(sc, tpr);
	x &= ~OPENPIC_TPR_MASK;
	x |= pri;
	openpic_write(sc, tpr, x);
	sched_unpin();
}

int
openpic_common_attach(device_t dev, uint32_t node)
{
	struct openpic_softc *sc;
	u_int     cpu, ipi, irq;
	u_int32_t x;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	sc->sc_rid = 0;
	sc->sc_memr = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->sc_rid,
	    RF_ACTIVE);

	if (sc->sc_memr == NULL) {
		device_printf(dev, "Could not alloc mem resource!\n");
		return (ENXIO);
	}

	sc->sc_bt = rman_get_bustag(sc->sc_memr);
	sc->sc_bh = rman_get_bushandle(sc->sc_memr);

	/* Reset the PIC */
	x = openpic_read(sc, OPENPIC_CONFIG);
	x |= OPENPIC_CONFIG_RESET;
	openpic_write(sc, OPENPIC_CONFIG, x);

	while (openpic_read(sc, OPENPIC_CONFIG) & OPENPIC_CONFIG_RESET) {
		powerpc_sync();
		DELAY(100);
	}

	/* Check if this is a cascaded PIC */
	sc->sc_irq = 0;
	sc->sc_intr = NULL;
	do {
		struct resource_list *rl;

		rl = BUS_GET_RESOURCE_LIST(device_get_parent(dev), dev);
		if (rl == NULL)
			break;
		if (resource_list_find(rl, SYS_RES_IRQ, 0) == NULL)
			break;

		sc->sc_intr = bus_alloc_resource_any(dev, SYS_RES_IRQ,
		    &sc->sc_irq, RF_ACTIVE);

		/* XXX Cascaded PICs pass NULL trapframes! */
		bus_setup_intr(dev, sc->sc_intr, INTR_TYPE_MISC | INTR_MPSAFE,
		    openpic_intr, NULL, dev, &sc->sc_icookie);
	} while (0);

	/* Reset the PIC */
	x = openpic_read(sc, OPENPIC_CONFIG);
	x |= OPENPIC_CONFIG_RESET;
	openpic_write(sc, OPENPIC_CONFIG, x);

	while (openpic_read(sc, OPENPIC_CONFIG) & OPENPIC_CONFIG_RESET) {
		powerpc_sync();
		DELAY(100);
	}

	x = openpic_read(sc, OPENPIC_FEATURE);
	switch (x & OPENPIC_FEATURE_VERSION_MASK) {
	case 1:
		sc->sc_version = "1.0";
		break;
	case 2:
		sc->sc_version = "1.2";
		break;
	case 3:
		sc->sc_version = "1.3";
		break;
	default:
		sc->sc_version = "unknown";
		break;
	}

	sc->sc_ncpu = ((x & OPENPIC_FEATURE_LAST_CPU_MASK) >>
	    OPENPIC_FEATURE_LAST_CPU_SHIFT) + 1;
	sc->sc_nirq = ((x & OPENPIC_FEATURE_LAST_IRQ_MASK) >>
	    OPENPIC_FEATURE_LAST_IRQ_SHIFT) + 1;

	/*
	 * PSIM seems to report 1 too many IRQs and CPUs
	 */
	if (sc->sc_psim) {
		sc->sc_nirq--;
		sc->sc_ncpu--;
	}

	if (bootverbose)
		device_printf(dev,
		    "Version %s, supports %d CPUs and %d irqs\n",
		    sc->sc_version, sc->sc_ncpu, sc->sc_nirq);

	for (cpu = 0; cpu < sc->sc_ncpu; cpu++)
		openpic_write(sc, OPENPIC_PCPU_TPR(cpu), 15);

	/* Reset and disable all interrupts. */
	for (irq = 0; irq < sc->sc_nirq; irq++) {
		x = irq;                /* irq == vector. */
		x |= OPENPIC_IMASK;
		x |= OPENPIC_POLARITY_NEGATIVE;
		x |= OPENPIC_SENSE_LEVEL;
		x |= 8 << OPENPIC_PRIORITY_SHIFT;
		openpic_write(sc, OPENPIC_SRC_VECTOR(irq), x);
	}

	/* Reset and disable all IPIs. */
	for (ipi = 0; ipi < 4; ipi++) {
		x = sc->sc_nirq + ipi;
		x |= OPENPIC_IMASK;
		x |= 15 << OPENPIC_PRIORITY_SHIFT;
		openpic_write(sc, OPENPIC_IPI_VECTOR(ipi), x);
	}

	/* we don't need 8259 passthrough mode */
	x = openpic_read(sc, OPENPIC_CONFIG);
	x |= OPENPIC_CONFIG_8259_PASSTHRU_DISABLE;
	openpic_write(sc, OPENPIC_CONFIG, x);

	/* send all interrupts to cpu 0 */
	for (irq = 0; irq < sc->sc_nirq; irq++)
		openpic_write(sc, OPENPIC_IDEST(irq), 1 << 0);

	/* clear all pending interrupts from cpu 0 */
	for (irq = 0; irq < sc->sc_nirq; irq++) {
		(void)openpic_read(sc, OPENPIC_PCPU_IACK(0));
		openpic_write(sc, OPENPIC_PCPU_EOI(0), 0);
	}

	for (cpu = 0; cpu < sc->sc_ncpu; cpu++)
		openpic_write(sc, OPENPIC_PCPU_TPR(cpu), 0);

	powerpc_register_pic(dev, node, sc->sc_nirq, 4, FALSE);

	/* If this is not a cascaded PIC, it must be the root PIC */
	if (sc->sc_intr == NULL)
		root_pic = dev;

	return (0);
}

/*
 * PIC I/F methods
 */

void
openpic_bind(device_t dev, u_int irq, cpuset_t cpumask, void **priv __unused)
{
	struct openpic_softc *sc;
	uint32_t mask;

	/* If we aren't directly connected to the CPU, this won't work */
	if (dev != root_pic)
		return;

	sc = device_get_softc(dev);

	/*
	 * XXX: openpic_write() is very special and just needs a 32 bits mask.
	 * For the moment, just play dirty and get the first half word.
	 */
	mask = cpumask.__bits[0] & 0xffffffff;
	if (sc->sc_quirks & OPENPIC_QUIRK_SINGLE_BIND) {
		int i = mftb() % CPU_COUNT(&cpumask);
		int cpu, ncpu;

		ncpu = 0;
		CPU_FOREACH(cpu) {
			if (!(mask & (1 << cpu)))
				continue;
			if (ncpu == i)
				break;
			ncpu++;
		}
		mask &= (1 << cpu);
	}

	openpic_write(sc, OPENPIC_IDEST(irq), mask);
}

void
openpic_config(device_t dev, u_int irq, enum intr_trigger trig,
    enum intr_polarity pol)
{
	struct openpic_softc *sc;
	uint32_t x;

	sc = device_get_softc(dev);
	x = openpic_read(sc, OPENPIC_SRC_VECTOR(irq));
	if (pol == INTR_POLARITY_LOW)
		x &= ~OPENPIC_POLARITY_POSITIVE;
	else
		x |= OPENPIC_POLARITY_POSITIVE;
	if (trig == INTR_TRIGGER_EDGE)
		x &= ~OPENPIC_SENSE_LEVEL;
	else
		x |= OPENPIC_SENSE_LEVEL;
	openpic_write(sc, OPENPIC_SRC_VECTOR(irq), x);
}

static int
openpic_intr(void *arg)
{
	device_t dev = (device_t)(arg);

	/* XXX Cascaded PICs do not pass non-NULL trapframes! */
	openpic_dispatch(dev, NULL);

	return (FILTER_HANDLED);
}

void
openpic_dispatch(device_t dev, struct trapframe *tf)
{
	struct openpic_softc *sc;
	u_int cpuid, vector;

	CTR1(KTR_INTR, "%s: got interrupt", __func__);

	cpuid = (dev == root_pic) ? PCPU_GET(cpuid) : 0;

	sc = device_get_softc(dev);
	while (1) {
		vector = openpic_read(sc, OPENPIC_PCPU_IACK(cpuid));
		vector &= OPENPIC_VECTOR_MASK;
		if (vector == 255)
			break;
		powerpc_dispatch_intr(vector, tf);
	}
}

void
openpic_enable(device_t dev, u_int irq, u_int vector, void **priv __unused)
{
	struct openpic_softc *sc;
	uint32_t x;

	sc = device_get_softc(dev);
	if (irq < sc->sc_nirq) {
		x = openpic_read(sc, OPENPIC_SRC_VECTOR(irq));
		x &= ~(OPENPIC_IMASK | OPENPIC_VECTOR_MASK);
		x |= vector;
		openpic_write(sc, OPENPIC_SRC_VECTOR(irq), x);
	} else {
		x = openpic_read(sc, OPENPIC_IPI_VECTOR(0));
		x &= ~(OPENPIC_IMASK | OPENPIC_VECTOR_MASK);
		x |= vector;
		openpic_write(sc, OPENPIC_IPI_VECTOR(0), x);
	}
}

void
openpic_eoi(device_t dev, u_int irq __unused, void *priv __unused)
{
	struct openpic_softc *sc;
	u_int cpuid;

	cpuid = (dev == root_pic) ? PCPU_GET(cpuid) : 0;

	sc = device_get_softc(dev);
	openpic_write(sc, OPENPIC_PCPU_EOI(cpuid), 0);
}

void
openpic_ipi(device_t dev, u_int cpu)
{
	struct openpic_softc *sc;

	KASSERT(dev == root_pic, ("Cannot send IPIs from non-root OpenPIC"));

	sc = device_get_softc(dev);
	sched_pin();
	openpic_write(sc, OPENPIC_PCPU_IPI_DISPATCH(PCPU_GET(cpuid), 0),
	    1u << cpu);
	sched_unpin();
}

void
openpic_mask(device_t dev, u_int irq, void *priv __unused)
{
	struct openpic_softc *sc;
	uint32_t x;

	sc = device_get_softc(dev);
	if (irq < sc->sc_nirq) {
		x = openpic_read(sc, OPENPIC_SRC_VECTOR(irq));
		x |= OPENPIC_IMASK;
		openpic_write(sc, OPENPIC_SRC_VECTOR(irq), x);
	} else {
		x = openpic_read(sc, OPENPIC_IPI_VECTOR(0));
		x |= OPENPIC_IMASK;
		openpic_write(sc, OPENPIC_IPI_VECTOR(0), x);
	}
}

void
openpic_unmask(device_t dev, u_int irq, void *priv __unused)
{
	struct openpic_softc *sc;
	uint32_t x;

	sc = device_get_softc(dev);
	if (irq < sc->sc_nirq) {
		x = openpic_read(sc, OPENPIC_SRC_VECTOR(irq));
		x &= ~OPENPIC_IMASK;
		openpic_write(sc, OPENPIC_SRC_VECTOR(irq), x);
	} else {
		x = openpic_read(sc, OPENPIC_IPI_VECTOR(0));
		x &= ~OPENPIC_IMASK;
		openpic_write(sc, OPENPIC_IPI_VECTOR(0), x);
	}
}

int
openpic_suspend(device_t dev)
{
	struct openpic_softc *sc;
	int i;

	sc = device_get_softc(dev);

	sc->sc_saved_config = bus_read_4(sc->sc_memr, OPENPIC_CONFIG);
	for (i = 0; i < 4; i++) {
		sc->sc_saved_ipis[i] = bus_read_4(sc->sc_memr, OPENPIC_IPI_VECTOR(i));
	}

	for (i = 0; i < 4; i++) {
		sc->sc_saved_prios[i] = bus_read_4(sc->sc_memr, OPENPIC_PCPU_TPR(i));
	}

	for (i = 0; i < OPENPIC_TIMERS; i++) {
		sc->sc_saved_timers[i].tcnt = bus_read_4(sc->sc_memr, OPENPIC_TCNT(i));
		sc->sc_saved_timers[i].tbase = bus_read_4(sc->sc_memr, OPENPIC_TBASE(i));
		sc->sc_saved_timers[i].tvec = bus_read_4(sc->sc_memr, OPENPIC_TVEC(i));
		sc->sc_saved_timers[i].tdst = bus_read_4(sc->sc_memr, OPENPIC_TDST(i));
	}

	for (i = 0; i < OPENPIC_SRC_VECTOR_COUNT; i++)
		sc->sc_saved_vectors[i] =
		    bus_read_4(sc->sc_memr, OPENPIC_SRC_VECTOR(i)) & ~OPENPIC_ACTIVITY;

	return (0);
}

int
openpic_resume(device_t dev)
{
    	struct openpic_softc *sc;
    	int i;

    	sc = device_get_softc(dev);

	sc->sc_saved_config = bus_read_4(sc->sc_memr, OPENPIC_CONFIG);
	for (i = 0; i < 4; i++) {
		bus_write_4(sc->sc_memr, OPENPIC_IPI_VECTOR(i), sc->sc_saved_ipis[i]);
	}

	for (i = 0; i < 4; i++) {
		bus_write_4(sc->sc_memr, OPENPIC_PCPU_TPR(i), sc->sc_saved_prios[i]);
	}

	for (i = 0; i < OPENPIC_TIMERS; i++) {
		bus_write_4(sc->sc_memr, OPENPIC_TCNT(i), sc->sc_saved_timers[i].tcnt);
		bus_write_4(sc->sc_memr, OPENPIC_TBASE(i), sc->sc_saved_timers[i].tbase);
		bus_write_4(sc->sc_memr, OPENPIC_TVEC(i), sc->sc_saved_timers[i].tvec);
		bus_write_4(sc->sc_memr, OPENPIC_TDST(i), sc->sc_saved_timers[i].tdst);
	}

	for (i = 0; i < OPENPIC_SRC_VECTOR_COUNT; i++)
		bus_write_4(sc->sc_memr, OPENPIC_SRC_VECTOR(i), sc->sc_saved_vectors[i]);

	return (0);
}
