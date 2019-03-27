/*
 * Copyright 2015 Andrew Turner.
 * Copyright 2016 Svatopluk Kraus
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpuset.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#ifdef SMP
#include <sys/smp.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/resource.h>
#ifdef SMP
#include <machine/smp.h>
#endif

#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_bus.h>

#include "pic_if.h"

#define	BCM_LINTC_CONTROL_REG		0x00
#define	BCM_LINTC_PRESCALER_REG		0x08
#define	BCM_LINTC_GPU_ROUTING_REG	0x0c
#define	BCM_LINTC_PMU_ROUTING_SET_REG	0x10
#define	BCM_LINTC_PMU_ROUTING_CLR_REG	0x14
#define	BCM_LINTC_TIMER_CFG_REG(n)	(0x40 + (n) * 4)
#define	BCM_LINTC_MBOX_CFG_REG(n)	(0x50 + (n) * 4)
#define	BCM_LINTC_PENDING_REG(n)	(0x60 + (n) * 4)
#define	BCM_LINTC_MBOX0_SET_REG(n)	(0x80 + (n) * 16)
#define	BCM_LINTC_MBOX1_SET_REG(n)	(0x84 + (n) * 16)
#define	BCM_LINTC_MBOX2_SET_REG(n)	(0x88 + (n) * 16)
#define	BCM_LINTC_MBOX3_SET_REG(n)	(0x8C + (n) * 16)
#define	BCM_LINTC_MBOX0_CLR_REG(n)	(0xC0 + (n) * 16)
#define	BCM_LINTC_MBOX1_CLR_REG(n)	(0xC4 + (n) * 16)
#define	BCM_LINTC_MBOX2_CLR_REG(n)	(0xC8 + (n) * 16)
#define	BCM_LINTC_MBOX3_CLR_REG(n)	(0xCC + (n) * 16)

/* Prescaler Register */
#define	BCM_LINTC_PSR_19_2		0x80000000	/* 19.2 MHz */

/* GPU Interrupt Routing Register */
#define	BCM_LINTC_GIRR_IRQ_CORE(n)	(n)
#define	BCM_LINTC_GIRR_FIQ_CORE(n)	((n) << 2)

/* PMU Interrupt Routing Register */
#define	BCM_LINTC_PIRR_IRQ_EN_CORE(n)	(1 << (n))
#define	BCM_LINTC_PIRR_FIQ_EN_CORE(n)	(1 << ((n) + 4))

/* Timer Config Register */
#define	BCM_LINTC_TCR_IRQ_EN_TIMER(n)	(1 << (n))
#define	BCM_LINTC_TCR_FIQ_EN_TIMER(n)	(1 << ((n) + 4))

/* MBOX Config Register */
#define	BCM_LINTC_MCR_IRQ_EN_MBOX(n)	(1 << (n))
#define	BCM_LINTC_MCR_FIQ_EN_MBOX(n)	(1 << ((n) + 4))

#define	BCM_LINTC_CNTPSIRQ_IRQ		0
#define	BCM_LINTC_CNTPNSIRQ_IRQ		1
#define	BCM_LINTC_CNTHPIRQ_IRQ		2
#define	BCM_LINTC_CNTVIRQ_IRQ		3
#define	BCM_LINTC_MBOX0_IRQ		4
#define	BCM_LINTC_MBOX1_IRQ		5
#define	BCM_LINTC_MBOX2_IRQ		6
#define	BCM_LINTC_MBOX3_IRQ		7
#define	BCM_LINTC_GPU_IRQ		8
#define	BCM_LINTC_PMU_IRQ		9
#define	BCM_LINTC_AXI_IRQ		10
#define	BCM_LINTC_LTIMER_IRQ		11

#define	BCM_LINTC_NIRQS			12

#define	BCM_LINTC_TIMER0_IRQ		BCM_LINTC_CNTPSIRQ_IRQ
#define	BCM_LINTC_TIMER1_IRQ		BCM_LINTC_CNTPNSIRQ_IRQ
#define	BCM_LINTC_TIMER2_IRQ		BCM_LINTC_CNTHPIRQ_IRQ
#define	BCM_LINTC_TIMER3_IRQ		BCM_LINTC_CNTVIRQ_IRQ

#define	BCM_LINTC_TIMER0_IRQ_MASK	(1 << BCM_LINTC_TIMER0_IRQ)
#define	BCM_LINTC_TIMER1_IRQ_MASK	(1 << BCM_LINTC_TIMER1_IRQ)
#define	BCM_LINTC_TIMER2_IRQ_MASK	(1 << BCM_LINTC_TIMER2_IRQ)
#define	BCM_LINTC_TIMER3_IRQ_MASK	(1 << BCM_LINTC_TIMER3_IRQ)
#define	BCM_LINTC_MBOX0_IRQ_MASK	(1 << BCM_LINTC_MBOX0_IRQ)
#define	BCM_LINTC_GPU_IRQ_MASK		(1 << BCM_LINTC_GPU_IRQ)
#define	BCM_LINTC_PMU_IRQ_MASK		(1 << BCM_LINTC_PMU_IRQ)

#define	BCM_LINTC_UP_PENDING_MASK	\
    (BCM_LINTC_TIMER0_IRQ_MASK |	\
     BCM_LINTC_TIMER1_IRQ_MASK |	\
     BCM_LINTC_TIMER2_IRQ_MASK |	\
     BCM_LINTC_TIMER3_IRQ_MASK |	\
     BCM_LINTC_GPU_IRQ_MASK |		\
     BCM_LINTC_PMU_IRQ_MASK)

#define	BCM_LINTC_SMP_PENDING_MASK	\
    (BCM_LINTC_UP_PENDING_MASK |	\
     BCM_LINTC_MBOX0_IRQ_MASK)

#ifdef SMP
#define BCM_LINTC_PENDING_MASK		BCM_LINTC_SMP_PENDING_MASK
#else
#define BCM_LINTC_PENDING_MASK		BCM_LINTC_UP_PENDING_MASK
#endif

struct bcm_lintc_irqsrc {
	struct intr_irqsrc	bli_isrc;
	u_int			bli_irq;
	union {
		u_int		bli_mask;	/* for timers */
		u_int		bli_value;	/* for GPU */
	};
};

struct bcm_lintc_softc {
	device_t		bls_dev;
	struct mtx		bls_mtx;
	struct resource *	bls_mem;
	bus_space_tag_t		bls_bst;
	bus_space_handle_t	bls_bsh;
	struct bcm_lintc_irqsrc	bls_isrcs[BCM_LINTC_NIRQS];
};

static struct bcm_lintc_softc *bcm_lintc_sc;

#ifdef SMP
#define BCM_LINTC_NIPIS		32	/* only mailbox 0 is used for IPI */
CTASSERT(INTR_IPI_COUNT <= BCM_LINTC_NIPIS);
#endif

#define	BCM_LINTC_LOCK(sc)		mtx_lock_spin(&(sc)->bls_mtx)
#define	BCM_LINTC_UNLOCK(sc)		mtx_unlock_spin(&(sc)->bls_mtx)
#define	BCM_LINTC_LOCK_INIT(sc)		mtx_init(&(sc)->bls_mtx,	\
    device_get_nameunit((sc)->bls_dev), "bmc_local_intc", MTX_SPIN)
#define	BCM_LINTC_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->bls_mtx)

#define	bcm_lintc_read_4(sc, reg)		\
    bus_space_read_4((sc)->bls_bst, (sc)->bls_bsh, (reg))
#define	bcm_lintc_write_4(sc, reg, val)		\
    bus_space_write_4((sc)->bls_bst, (sc)->bls_bsh, (reg), (val))

static inline void
bcm_lintc_rwreg_clr(struct bcm_lintc_softc *sc, uint32_t reg,
    uint32_t mask)
{

	bcm_lintc_write_4(sc, reg, bcm_lintc_read_4(sc, reg) & ~mask);
}

static inline void
bcm_lintc_rwreg_set(struct bcm_lintc_softc *sc, uint32_t reg,
    uint32_t mask)
{

	bcm_lintc_write_4(sc, reg, bcm_lintc_read_4(sc, reg) | mask);
}

static void
bcm_lintc_timer_mask(struct bcm_lintc_softc *sc, struct bcm_lintc_irqsrc *bli)
{
	cpuset_t *cpus;
	uint32_t cpu;

	cpus = &bli->bli_isrc.isrc_cpu;

	BCM_LINTC_LOCK(sc);
	for (cpu = 0; cpu < 4; cpu++)
		if (CPU_ISSET(cpu, cpus))
			bcm_lintc_rwreg_clr(sc, BCM_LINTC_TIMER_CFG_REG(cpu),
			    bli->bli_mask);
	BCM_LINTC_UNLOCK(sc);
}

static void
bcm_lintc_timer_unmask(struct bcm_lintc_softc *sc, struct bcm_lintc_irqsrc *bli)
{
	cpuset_t *cpus;
	uint32_t cpu;

	cpus = &bli->bli_isrc.isrc_cpu;

	BCM_LINTC_LOCK(sc);
	for (cpu = 0; cpu < 4; cpu++)
		if (CPU_ISSET(cpu, cpus))
			bcm_lintc_rwreg_set(sc, BCM_LINTC_TIMER_CFG_REG(cpu),
			    bli->bli_mask);
	BCM_LINTC_UNLOCK(sc);
}

static inline void
bcm_lintc_gpu_mask(struct bcm_lintc_softc *sc, struct bcm_lintc_irqsrc *bli)
{

	/* It's accessed just and only by one core. */
	bcm_lintc_write_4(sc, BCM_LINTC_GPU_ROUTING_REG, 0);
}

static inline void
bcm_lintc_gpu_unmask(struct bcm_lintc_softc *sc, struct bcm_lintc_irqsrc *bli)
{

	/* It's accessed just and only by one core. */
	bcm_lintc_write_4(sc, BCM_LINTC_GPU_ROUTING_REG, bli->bli_value);
}

static inline void
bcm_lintc_pmu_mask(struct bcm_lintc_softc *sc, struct bcm_lintc_irqsrc *bli)
{
	cpuset_t *cpus;
	uint32_t cpu, mask;

	mask = 0;
	cpus = &bli->bli_isrc.isrc_cpu;

	BCM_LINTC_LOCK(sc);
	for (cpu = 0; cpu < 4; cpu++)
		if (CPU_ISSET(cpu, cpus))
			mask |= BCM_LINTC_PIRR_IRQ_EN_CORE(cpu);
	/* Write-clear register. */
	bcm_lintc_write_4(sc, BCM_LINTC_PMU_ROUTING_CLR_REG, mask);
	BCM_LINTC_UNLOCK(sc);
}

static inline void
bcm_lintc_pmu_unmask(struct bcm_lintc_softc *sc, struct bcm_lintc_irqsrc *bli)
{
	cpuset_t *cpus;
	uint32_t cpu, mask;

	mask = 0;
	cpus = &bli->bli_isrc.isrc_cpu;

	BCM_LINTC_LOCK(sc);
	for (cpu = 0; cpu < 4; cpu++)
		if (CPU_ISSET(cpu, cpus))
			mask |= BCM_LINTC_PIRR_IRQ_EN_CORE(cpu);
	/* Write-set register. */
	bcm_lintc_write_4(sc, BCM_LINTC_PMU_ROUTING_SET_REG, mask);
	BCM_LINTC_UNLOCK(sc);
}

static void
bcm_lintc_mask(struct bcm_lintc_softc *sc, struct bcm_lintc_irqsrc *bli)
{

	switch (bli->bli_irq) {
	case BCM_LINTC_TIMER0_IRQ:
	case BCM_LINTC_TIMER1_IRQ:
	case BCM_LINTC_TIMER2_IRQ:
	case BCM_LINTC_TIMER3_IRQ:
		bcm_lintc_timer_mask(sc, bli);
		return;
	case BCM_LINTC_MBOX0_IRQ:
	case BCM_LINTC_MBOX1_IRQ:
	case BCM_LINTC_MBOX2_IRQ:
	case BCM_LINTC_MBOX3_IRQ:
		return;
	case BCM_LINTC_GPU_IRQ:
		bcm_lintc_gpu_mask(sc, bli);
		return;
	case BCM_LINTC_PMU_IRQ:
		bcm_lintc_pmu_mask(sc, bli);
		return;
	default:
		panic("%s: not implemented for irq %u", __func__, bli->bli_irq);
	}
}

static void
bcm_lintc_unmask(struct bcm_lintc_softc *sc, struct bcm_lintc_irqsrc *bli)
{

	switch (bli->bli_irq) {
	case BCM_LINTC_TIMER0_IRQ:
	case BCM_LINTC_TIMER1_IRQ:
	case BCM_LINTC_TIMER2_IRQ:
	case BCM_LINTC_TIMER3_IRQ:
		bcm_lintc_timer_unmask(sc, bli);
		return;
	case BCM_LINTC_MBOX0_IRQ:
	case BCM_LINTC_MBOX1_IRQ:
	case BCM_LINTC_MBOX2_IRQ:
	case BCM_LINTC_MBOX3_IRQ:
		return;
	case BCM_LINTC_GPU_IRQ:
		bcm_lintc_gpu_unmask(sc, bli);
		return;
	case BCM_LINTC_PMU_IRQ:
		bcm_lintc_pmu_unmask(sc, bli);
		return;
	default:
		panic("%s: not implemented for irq %u", __func__, bli->bli_irq);
	}
}

#ifdef SMP
static inline void
bcm_lintc_ipi_write(struct bcm_lintc_softc *sc, cpuset_t cpus, u_int ipi)
{
	u_int cpu;
	uint32_t mask;

	mask = 1 << ipi;
	for (cpu = 0; cpu < mp_ncpus; cpu++)
		if (CPU_ISSET(cpu, &cpus))
			bcm_lintc_write_4(sc, BCM_LINTC_MBOX0_SET_REG(cpu),
			    mask);
}

static inline void
bcm_lintc_ipi_dispatch(struct bcm_lintc_softc *sc, u_int cpu,
    struct trapframe *tf)
{
	u_int ipi;
	uint32_t mask;

	mask = bcm_lintc_read_4(sc, BCM_LINTC_MBOX0_CLR_REG(cpu));
	if (mask == 0) {
		device_printf(sc->bls_dev, "Spurious ipi detected\n");
		return;
	}

	for (ipi = 0; mask != 0; mask >>= 1, ipi++) {
		if ((mask & 0x01) == 0)
			continue;
		/*
		 * Clear an IPI before dispatching to not miss anyone
		 * and make sure that it's observed by everybody.
		 */
		bcm_lintc_write_4(sc, BCM_LINTC_MBOX0_CLR_REG(cpu), 1 << ipi);
#if defined(__aarch64__)
		dsb(sy);
#else
		dsb();
#endif
		intr_ipi_dispatch(ipi, tf);
	}
}
#endif

static inline void
bcm_lintc_irq_dispatch(struct bcm_lintc_softc *sc, u_int irq,
    struct trapframe *tf)
{
	struct bcm_lintc_irqsrc *bli;

	bli = &sc->bls_isrcs[irq];
	if (intr_isrc_dispatch(&bli->bli_isrc, tf) != 0)
		device_printf(sc->bls_dev, "Stray irq %u detected\n", irq);
}

static int
bcm_lintc_intr(void *arg)
{
	struct bcm_lintc_softc *sc;
	u_int cpu;
	uint32_t num, reg;
	struct trapframe *tf;

	sc = arg;
	cpu = PCPU_GET(cpuid);
	tf = curthread->td_intr_frame;

	for (num = 0; ; num++) {
		reg = bcm_lintc_read_4(sc, BCM_LINTC_PENDING_REG(cpu));
		if ((reg & BCM_LINTC_PENDING_MASK) == 0)
			break;
#ifdef SMP
		if (reg & BCM_LINTC_MBOX0_IRQ_MASK)
			bcm_lintc_ipi_dispatch(sc, cpu, tf);
#endif
		if (reg & BCM_LINTC_TIMER0_IRQ_MASK)
			bcm_lintc_irq_dispatch(sc, BCM_LINTC_TIMER0_IRQ, tf);
		if (reg & BCM_LINTC_TIMER1_IRQ_MASK)
			bcm_lintc_irq_dispatch(sc, BCM_LINTC_TIMER1_IRQ, tf);
		if (reg & BCM_LINTC_TIMER2_IRQ_MASK)
			bcm_lintc_irq_dispatch(sc, BCM_LINTC_TIMER2_IRQ, tf);
		if (reg & BCM_LINTC_TIMER3_IRQ_MASK)
			bcm_lintc_irq_dispatch(sc, BCM_LINTC_TIMER3_IRQ, tf);
		if (reg & BCM_LINTC_GPU_IRQ_MASK)
			bcm_lintc_irq_dispatch(sc, BCM_LINTC_GPU_IRQ, tf);
		if (reg & BCM_LINTC_PMU_IRQ_MASK)
			bcm_lintc_irq_dispatch(sc, BCM_LINTC_PMU_IRQ, tf);

		arm_irq_memory_barrier(0); /* XXX */
	}
	reg &= ~BCM_LINTC_PENDING_MASK;
	if (reg != 0)
		device_printf(sc->bls_dev, "Unknown interrupt(s) %x\n", reg);
	else if (num == 0)
		device_printf(sc->bls_dev, "Spurious interrupt detected\n");

	return (FILTER_HANDLED);
}

static void
bcm_lintc_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{

	bcm_lintc_mask(device_get_softc(dev), (struct bcm_lintc_irqsrc *)isrc);
}

static void
bcm_lintc_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct bcm_lintc_irqsrc *bli = (struct bcm_lintc_irqsrc *)isrc;

	arm_irq_memory_barrier(bli->bli_irq);
	bcm_lintc_unmask(device_get_softc(dev), bli);
}

static int
bcm_lintc_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct intr_map_data_fdt *daf;
	struct bcm_lintc_softc *sc;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;
	if (daf->ncells > 2 || daf->cells[0] >= BCM_LINTC_NIRQS)
		return (EINVAL);

	/* TODO: handle IRQ type here */

	sc = device_get_softc(dev);
	*isrcp = &sc->bls_isrcs[daf->cells[0]].bli_isrc;
	return (0);
}

static void
bcm_lintc_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct bcm_lintc_irqsrc *bli = (struct bcm_lintc_irqsrc *)isrc;

	if (bli->bli_irq == BCM_LINTC_GPU_IRQ)
		bcm_lintc_gpu_mask(device_get_softc(dev), bli);
	else {
		/*
		 * Handler for PPI interrupt does not make sense much unless
		 * there is one bound ithread for each core for it. Thus the
		 * interrupt can be masked on current core only while ithread
		 * bounded to this core ensures unmasking on the same core.
		 */
		panic ("%s: handlers are not supported", __func__);
	}
}

static void
bcm_lintc_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct bcm_lintc_irqsrc *bli = (struct bcm_lintc_irqsrc *)isrc;

	if (bli->bli_irq == BCM_LINTC_GPU_IRQ)
		bcm_lintc_gpu_unmask(device_get_softc(dev), bli);
	else {
		/* See comment in bcm_lintc_pre_ithread(). */
		panic ("%s: handlers are not supported", __func__);
	}
}

static void
bcm_lintc_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
}

static int
bcm_lintc_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct bcm_lintc_softc *sc;

	if (isrc->isrc_handlers == 0 && isrc->isrc_flags & INTR_ISRCF_PPI) {
		sc = device_get_softc(dev);
		BCM_LINTC_LOCK(sc);
		CPU_SET(PCPU_GET(cpuid), &isrc->isrc_cpu);
		BCM_LINTC_UNLOCK(sc);
	}
	return (0);
}

#ifdef SMP
static void
bcm_lintc_init_rwreg_on_ap(struct bcm_lintc_softc *sc, u_int cpu, u_int irq,
    uint32_t reg, uint32_t mask)
{

	if (intr_isrc_init_on_cpu(&sc->bls_isrcs[irq].bli_isrc, cpu))
		bcm_lintc_rwreg_set(sc, reg, mask);
}

static void
bcm_lintc_init_pmu_on_ap(struct bcm_lintc_softc *sc, u_int cpu)
{
	struct intr_irqsrc *isrc = &sc->bls_isrcs[BCM_LINTC_PMU_IRQ].bli_isrc;

	if (intr_isrc_init_on_cpu(isrc, cpu)) {
		/* Write-set register. */
		bcm_lintc_write_4(sc, BCM_LINTC_PMU_ROUTING_SET_REG,
		    BCM_LINTC_PIRR_IRQ_EN_CORE(cpu));
	}
}

static void
bcm_lintc_init_secondary(device_t dev)
{
	u_int cpu;
	struct bcm_lintc_softc *sc;

	cpu = PCPU_GET(cpuid);
	sc = device_get_softc(dev);

	BCM_LINTC_LOCK(sc);
	bcm_lintc_init_rwreg_on_ap(sc, cpu, BCM_LINTC_TIMER0_IRQ,
	    BCM_LINTC_TIMER_CFG_REG(cpu), BCM_LINTC_TCR_IRQ_EN_TIMER(0));
	bcm_lintc_init_rwreg_on_ap(sc, cpu, BCM_LINTC_TIMER1_IRQ,
	    BCM_LINTC_TIMER_CFG_REG(cpu), BCM_LINTC_TCR_IRQ_EN_TIMER(1));
	bcm_lintc_init_rwreg_on_ap(sc, cpu, BCM_LINTC_TIMER2_IRQ,
	    BCM_LINTC_TIMER_CFG_REG(cpu), BCM_LINTC_TCR_IRQ_EN_TIMER(2));
	bcm_lintc_init_rwreg_on_ap(sc, cpu, BCM_LINTC_TIMER3_IRQ,
	    BCM_LINTC_TIMER_CFG_REG(cpu), BCM_LINTC_TCR_IRQ_EN_TIMER(3));
	bcm_lintc_init_pmu_on_ap(sc, cpu);
	BCM_LINTC_UNLOCK(sc);
}

static void
bcm_lintc_ipi_send(device_t dev, struct intr_irqsrc *isrc, cpuset_t cpus,
    u_int ipi)
{
	struct bcm_lintc_softc *sc = device_get_softc(dev);

	KASSERT(isrc == &sc->bls_isrcs[BCM_LINTC_MBOX0_IRQ].bli_isrc,
	    ("%s: bad ISRC %p argument", __func__, isrc));
	bcm_lintc_ipi_write(sc, cpus, ipi);
}

static int
bcm_lintc_ipi_setup(device_t dev, u_int ipi, struct intr_irqsrc **isrcp)
{
	struct bcm_lintc_softc *sc = device_get_softc(dev);

	KASSERT(ipi < BCM_LINTC_NIPIS, ("%s: too high ipi %u", __func__, ipi));

	*isrcp = &sc->bls_isrcs[BCM_LINTC_MBOX0_IRQ].bli_isrc;
	return (0);
}
#endif

static int
bcm_lintc_pic_attach(struct bcm_lintc_softc *sc)
{
	struct bcm_lintc_irqsrc *bisrcs;
	struct intr_pic *pic;
	int error;
	u_int flags;
	uint32_t irq;
	const char *name;
	intptr_t xref;

	bisrcs = sc->bls_isrcs;
	name = device_get_nameunit(sc->bls_dev);
	for (irq = 0; irq < BCM_LINTC_NIRQS; irq++) {
		bisrcs[irq].bli_irq = irq;
		switch (irq) {
		case BCM_LINTC_TIMER0_IRQ:
			bisrcs[irq].bli_mask = BCM_LINTC_TCR_IRQ_EN_TIMER(0);
			flags = INTR_ISRCF_PPI;
			break;
		case BCM_LINTC_TIMER1_IRQ:
			bisrcs[irq].bli_mask = BCM_LINTC_TCR_IRQ_EN_TIMER(1);
			flags = INTR_ISRCF_PPI;
			break;
		case BCM_LINTC_TIMER2_IRQ:
			bisrcs[irq].bli_mask = BCM_LINTC_TCR_IRQ_EN_TIMER(2);
			flags = INTR_ISRCF_PPI;
			break;
		case BCM_LINTC_TIMER3_IRQ:
			bisrcs[irq].bli_mask = BCM_LINTC_TCR_IRQ_EN_TIMER(3);
			flags = INTR_ISRCF_PPI;
			break;
		case BCM_LINTC_MBOX0_IRQ:
		case BCM_LINTC_MBOX1_IRQ:
		case BCM_LINTC_MBOX2_IRQ:
		case BCM_LINTC_MBOX3_IRQ:
			bisrcs[irq].bli_value = 0;	/* not used */
			flags = INTR_ISRCF_IPI;
			break;
		case BCM_LINTC_GPU_IRQ:
			bisrcs[irq].bli_value = BCM_LINTC_GIRR_IRQ_CORE(0);
			flags = 0;
			break;
		case BCM_LINTC_PMU_IRQ:
			bisrcs[irq].bli_value = 0;	/* not used */
			flags = INTR_ISRCF_PPI;
			break;
		default:
			bisrcs[irq].bli_value = 0;	/* not used */
			flags = 0;
			break;
		}

		error = intr_isrc_register(&bisrcs[irq].bli_isrc, sc->bls_dev,
		    flags, "%s,%u", name, irq);
		if (error != 0)
			return (error);
	}

	xref = OF_xref_from_node(ofw_bus_get_node(sc->bls_dev));
	pic = intr_pic_register(sc->bls_dev, xref);
	if (pic == NULL)
		return (ENXIO);

	return (intr_pic_claim_root(sc->bls_dev, xref, bcm_lintc_intr, sc, 0));
}

static int
bcm_lintc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "brcm,bcm2836-l1-intc"))
		return (ENXIO);
	device_set_desc(dev, "BCM2836 Interrupt Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
bcm_lintc_attach(device_t dev)
{
	struct bcm_lintc_softc *sc;
	int cpu, rid;

	sc = device_get_softc(dev);

	sc->bls_dev = dev;
	if (bcm_lintc_sc != NULL)
		return (ENXIO);

	rid = 0;
	sc->bls_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->bls_mem == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	sc->bls_bst = rman_get_bustag(sc->bls_mem);
	sc->bls_bsh = rman_get_bushandle(sc->bls_mem);

	bcm_lintc_write_4(sc, BCM_LINTC_CONTROL_REG, 0);
	bcm_lintc_write_4(sc, BCM_LINTC_PRESCALER_REG, BCM_LINTC_PSR_19_2);

	/* Disable all timers on all cores. */
	for (cpu = 0; cpu < 4; cpu++)
		bcm_lintc_write_4(sc, BCM_LINTC_TIMER_CFG_REG(cpu), 0);

#ifdef SMP
	/* Enable mailbox 0 on all cores used for IPI. */
	for (cpu = 0; cpu < 4; cpu++)
		bcm_lintc_write_4(sc, BCM_LINTC_MBOX_CFG_REG(cpu),
		    BCM_LINTC_MCR_IRQ_EN_MBOX(0));
#endif

	if (bcm_lintc_pic_attach(sc) != 0) {
		device_printf(dev, "could not attach PIC\n");
		return (ENXIO);
	}

	BCM_LINTC_LOCK_INIT(sc);
	bcm_lintc_sc = sc;
	return (0);
}

static device_method_t bcm_lintc_methods[] = {
	DEVMETHOD(device_probe,		bcm_lintc_probe),
	DEVMETHOD(device_attach,	bcm_lintc_attach),

	DEVMETHOD(pic_disable_intr,	bcm_lintc_disable_intr),
	DEVMETHOD(pic_enable_intr,	bcm_lintc_enable_intr),
	DEVMETHOD(pic_map_intr,		bcm_lintc_map_intr),
	DEVMETHOD(pic_post_filter,	bcm_lintc_post_filter),
	DEVMETHOD(pic_post_ithread,	bcm_lintc_post_ithread),
	DEVMETHOD(pic_pre_ithread,	bcm_lintc_pre_ithread),
	DEVMETHOD(pic_setup_intr,	bcm_lintc_setup_intr),
#ifdef SMP
	DEVMETHOD(pic_init_secondary,	bcm_lintc_init_secondary),
	DEVMETHOD(pic_ipi_send,		bcm_lintc_ipi_send),
	DEVMETHOD(pic_ipi_setup,	bcm_lintc_ipi_setup),
#endif

	DEVMETHOD_END
};

static driver_t bcm_lintc_driver = {
	"local_intc",
	bcm_lintc_methods,
	sizeof(struct bcm_lintc_softc),
};

static devclass_t bcm_lintc_devclass;

EARLY_DRIVER_MODULE(local_intc, simplebus, bcm_lintc_driver, bcm_lintc_devclass,
    0, 0, BUS_PASS_INTERRUPT);
