// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Generic Interrupt Controller (GIC) v3 support
 */

#include <linux/sizes.h>

#include "kvm_util.h"
#include "processor.h"
#include "delay.h"

#include "gic_v3.h"
#include "gic_private.h"

struct gicv3_data {
	void *dist_base;
	void *redist_base[GICV3_MAX_CPUS];
	unsigned int nr_cpus;
	unsigned int nr_spis;
};

#define sgi_base_from_redist(redist_base) (redist_base + SZ_64K)

enum gicv3_intid_range {
	SGI_RANGE,
	PPI_RANGE,
	SPI_RANGE,
	INVALID_RANGE,
};

static struct gicv3_data gicv3_data;

static void gicv3_gicd_wait_for_rwp(void)
{
	unsigned int count = 100000; /* 1s */

	while (readl(gicv3_data.dist_base + GICD_CTLR) & GICD_CTLR_RWP) {
		GUEST_ASSERT(count--);
		udelay(10);
	}
}

static void gicv3_gicr_wait_for_rwp(void *redist_base)
{
	unsigned int count = 100000; /* 1s */

	while (readl(redist_base + GICR_CTLR) & GICR_CTLR_RWP) {
		GUEST_ASSERT(count--);
		udelay(10);
	}
}

static enum gicv3_intid_range get_intid_range(unsigned int intid)
{
	switch (intid) {
	case 0 ... 15:
		return SGI_RANGE;
	case 16 ... 31:
		return PPI_RANGE;
	case 32 ... 1019:
		return SPI_RANGE;
	}

	/* We should not be reaching here */
	GUEST_ASSERT(0);

	return INVALID_RANGE;
}

static uint64_t gicv3_read_iar(void)
{
	uint64_t irqstat = read_sysreg_s(SYS_ICC_IAR1_EL1);

	dsb(sy);
	return irqstat;
}

static void gicv3_write_eoir(uint32_t irq)
{
	write_sysreg_s(irq, SYS_ICC_EOIR1_EL1);
	isb();
}

static void
gicv3_config_irq(unsigned int intid, unsigned int offset)
{
	uint32_t cpu = guest_get_vcpuid();
	uint32_t mask = 1 << (intid % 32);
	enum gicv3_intid_range intid_range = get_intid_range(intid);
	void *reg;

	/* We care about 'cpu' only for SGIs or PPIs */
	if (intid_range == SGI_RANGE || intid_range == PPI_RANGE) {
		GUEST_ASSERT(cpu < gicv3_data.nr_cpus);

		reg = sgi_base_from_redist(gicv3_data.redist_base[cpu]) +
			offset;
		writel(mask, reg);
		gicv3_gicr_wait_for_rwp(gicv3_data.redist_base[cpu]);
	} else if (intid_range == SPI_RANGE) {
		reg = gicv3_data.dist_base + offset + (intid / 32) * 4;
		writel(mask, reg);
		gicv3_gicd_wait_for_rwp();
	} else {
		GUEST_ASSERT(0);
	}
}

static void gicv3_irq_enable(unsigned int intid)
{
	gicv3_config_irq(intid, GICD_ISENABLER);
}

static void gicv3_irq_disable(unsigned int intid)
{
	gicv3_config_irq(intid, GICD_ICENABLER);
}

static void gicv3_enable_redist(void *redist_base)
{
	uint32_t val = readl(redist_base + GICR_WAKER);
	unsigned int count = 100000; /* 1s */

	val &= ~GICR_WAKER_ProcessorSleep;
	writel(val, redist_base + GICR_WAKER);

	/* Wait until the processor is 'active' */
	while (readl(redist_base + GICR_WAKER) & GICR_WAKER_ChildrenAsleep) {
		GUEST_ASSERT(count--);
		udelay(10);
	}
}

static inline void *gicr_base_cpu(void *redist_base, uint32_t cpu)
{
	/* Align all the redistributors sequentially */
	return redist_base + cpu * SZ_64K * 2;
}

static void gicv3_cpu_init(unsigned int cpu, void *redist_base)
{
	void *sgi_base;
	unsigned int i;
	void *redist_base_cpu;

	GUEST_ASSERT(cpu < gicv3_data.nr_cpus);

	redist_base_cpu = gicr_base_cpu(redist_base, cpu);
	sgi_base = sgi_base_from_redist(redist_base_cpu);

	gicv3_enable_redist(redist_base_cpu);

	/*
	 * Mark all the SGI and PPI interrupts as non-secure Group-1.
	 * Also, deactivate and disable them.
	 */
	writel(~0, sgi_base + GICR_IGROUPR0);
	writel(~0, sgi_base + GICR_ICACTIVER0);
	writel(~0, sgi_base + GICR_ICENABLER0);

	/* Set a default priority for all the SGIs and PPIs */
	for (i = 0; i < 32; i += 4)
		writel(GICD_INT_DEF_PRI_X4,
				sgi_base + GICR_IPRIORITYR0 + i);

	gicv3_gicr_wait_for_rwp(redist_base_cpu);

	/* Enable the GIC system register (ICC_*) access */
	write_sysreg_s(read_sysreg_s(SYS_ICC_SRE_EL1) | ICC_SRE_EL1_SRE,
			SYS_ICC_SRE_EL1);

	/* Set a default priority threshold */
	write_sysreg_s(ICC_PMR_DEF_PRIO, SYS_ICC_PMR_EL1);

	/* Enable non-secure Group-1 interrupts */
	write_sysreg_s(ICC_IGRPEN1_EL1_ENABLE, SYS_ICC_GRPEN1_EL1);

	gicv3_data.redist_base[cpu] = redist_base_cpu;
}

static void gicv3_dist_init(void)
{
	void *dist_base = gicv3_data.dist_base;
	unsigned int i;

	/* Disable the distributor until we set things up */
	writel(0, dist_base + GICD_CTLR);
	gicv3_gicd_wait_for_rwp();

	/*
	 * Mark all the SPI interrupts as non-secure Group-1.
	 * Also, deactivate and disable them.
	 */
	for (i = 32; i < gicv3_data.nr_spis; i += 32) {
		writel(~0, dist_base + GICD_IGROUPR + i / 8);
		writel(~0, dist_base + GICD_ICACTIVER + i / 8);
		writel(~0, dist_base + GICD_ICENABLER + i / 8);
	}

	/* Set a default priority for all the SPIs */
	for (i = 32; i < gicv3_data.nr_spis; i += 4)
		writel(GICD_INT_DEF_PRI_X4,
				dist_base + GICD_IPRIORITYR + i);

	/* Wait for the settings to sync-in */
	gicv3_gicd_wait_for_rwp();

	/* Finally, enable the distributor globally with ARE */
	writel(GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1A |
			GICD_CTLR_ENABLE_G1, dist_base + GICD_CTLR);
	gicv3_gicd_wait_for_rwp();
}

static void gicv3_init(unsigned int nr_cpus, void *dist_base)
{
	GUEST_ASSERT(nr_cpus <= GICV3_MAX_CPUS);

	gicv3_data.nr_cpus = nr_cpus;
	gicv3_data.dist_base = dist_base;
	gicv3_data.nr_spis = GICD_TYPER_SPIS(
				readl(gicv3_data.dist_base + GICD_TYPER));
	if (gicv3_data.nr_spis > 1020)
		gicv3_data.nr_spis = 1020;

	/*
	 * Initialize only the distributor for now.
	 * The redistributor and CPU interfaces are initialized
	 * later for every PE.
	 */
	gicv3_dist_init();
}

const struct gic_common_ops gicv3_ops = {
	.gic_init = gicv3_init,
	.gic_cpu_init = gicv3_cpu_init,
	.gic_irq_enable = gicv3_irq_enable,
	.gic_irq_disable = gicv3_irq_disable,
	.gic_read_iar = gicv3_read_iar,
	.gic_write_eoir = gicv3_write_eoir,
};
