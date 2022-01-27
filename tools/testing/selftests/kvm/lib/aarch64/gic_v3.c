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

#define sgi_base_from_redist(redist_base)	(redist_base + SZ_64K)
#define DIST_BIT				(1U << 31)

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

static void gicv3_wait_for_rwp(uint32_t cpu_or_dist)
{
	if (cpu_or_dist & DIST_BIT)
		gicv3_gicd_wait_for_rwp();
	else
		gicv3_gicr_wait_for_rwp(gicv3_data.redist_base[cpu_or_dist]);
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

static void gicv3_write_dir(uint32_t irq)
{
	write_sysreg_s(irq, SYS_ICC_DIR_EL1);
	isb();
}

static void gicv3_set_priority_mask(uint64_t mask)
{
	write_sysreg_s(mask, SYS_ICC_PMR_EL1);
}

static void gicv3_set_eoi_split(bool split)
{
	uint32_t val;

	/*
	 * All other fields are read-only, so no need to read CTLR first. In
	 * fact, the kernel does the same.
	 */
	val = split ? (1U << 1) : 0;
	write_sysreg_s(val, SYS_ICC_CTLR_EL1);
	isb();
}

uint32_t gicv3_reg_readl(uint32_t cpu_or_dist, uint64_t offset)
{
	void *base = cpu_or_dist & DIST_BIT ? gicv3_data.dist_base
		: sgi_base_from_redist(gicv3_data.redist_base[cpu_or_dist]);
	return readl(base + offset);
}

void gicv3_reg_writel(uint32_t cpu_or_dist, uint64_t offset, uint32_t reg_val)
{
	void *base = cpu_or_dist & DIST_BIT ? gicv3_data.dist_base
		: sgi_base_from_redist(gicv3_data.redist_base[cpu_or_dist]);
	writel(reg_val, base + offset);
}

uint32_t gicv3_getl_fields(uint32_t cpu_or_dist, uint64_t offset, uint32_t mask)
{
	return gicv3_reg_readl(cpu_or_dist, offset) & mask;
}

void gicv3_setl_fields(uint32_t cpu_or_dist, uint64_t offset,
		uint32_t mask, uint32_t reg_val)
{
	uint32_t tmp = gicv3_reg_readl(cpu_or_dist, offset) & ~mask;

	tmp |= (reg_val & mask);
	gicv3_reg_writel(cpu_or_dist, offset, tmp);
}

/*
 * We use a single offset for the distributor and redistributor maps as they
 * have the same value in both. The only exceptions are registers that only
 * exist in one and not the other, like GICR_WAKER that doesn't exist in the
 * distributor map. Such registers are conveniently marked as reserved in the
 * map that doesn't implement it; like GICR_WAKER's offset of 0x0014 being
 * marked as "Reserved" in the Distributor map.
 */
static void gicv3_access_reg(uint32_t intid, uint64_t offset,
		uint32_t reg_bits, uint32_t bits_per_field,
		bool write, uint32_t *val)
{
	uint32_t cpu = guest_get_vcpuid();
	enum gicv3_intid_range intid_range = get_intid_range(intid);
	uint32_t fields_per_reg, index, mask, shift;
	uint32_t cpu_or_dist;

	GUEST_ASSERT(bits_per_field <= reg_bits);
	GUEST_ASSERT(!write || *val < (1U << bits_per_field));
	/*
	 * This function does not support 64 bit accesses. Just asserting here
	 * until we implement readq/writeq.
	 */
	GUEST_ASSERT(reg_bits == 32);

	fields_per_reg = reg_bits / bits_per_field;
	index = intid % fields_per_reg;
	shift = index * bits_per_field;
	mask = ((1U << bits_per_field) - 1) << shift;

	/* Set offset to the actual register holding intid's config. */
	offset += (intid / fields_per_reg) * (reg_bits / 8);

	cpu_or_dist = (intid_range == SPI_RANGE) ? DIST_BIT : cpu;

	if (write)
		gicv3_setl_fields(cpu_or_dist, offset, mask, *val << shift);
	*val = gicv3_getl_fields(cpu_or_dist, offset, mask) >> shift;
}

static void gicv3_write_reg(uint32_t intid, uint64_t offset,
		uint32_t reg_bits, uint32_t bits_per_field, uint32_t val)
{
	gicv3_access_reg(intid, offset, reg_bits,
			bits_per_field, true, &val);
}

static uint32_t gicv3_read_reg(uint32_t intid, uint64_t offset,
		uint32_t reg_bits, uint32_t bits_per_field)
{
	uint32_t val;

	gicv3_access_reg(intid, offset, reg_bits,
			bits_per_field, false, &val);
	return val;
}

static void gicv3_set_priority(uint32_t intid, uint32_t prio)
{
	gicv3_write_reg(intid, GICD_IPRIORITYR, 32, 8, prio);
}

/* Sets the intid to be level-sensitive or edge-triggered. */
static void gicv3_irq_set_config(uint32_t intid, bool is_edge)
{
	uint32_t val;

	/* N/A for private interrupts. */
	GUEST_ASSERT(get_intid_range(intid) == SPI_RANGE);
	val = is_edge ? 2 : 0;
	gicv3_write_reg(intid, GICD_ICFGR, 32, 2, val);
}

static void gicv3_irq_enable(uint32_t intid)
{
	bool is_spi = get_intid_range(intid) == SPI_RANGE;
	uint32_t cpu = guest_get_vcpuid();

	gicv3_write_reg(intid, GICD_ISENABLER, 32, 1, 1);
	gicv3_wait_for_rwp(is_spi ? DIST_BIT : cpu);
}

static void gicv3_irq_disable(uint32_t intid)
{
	bool is_spi = get_intid_range(intid) == SPI_RANGE;
	uint32_t cpu = guest_get_vcpuid();

	gicv3_write_reg(intid, GICD_ICENABLER, 32, 1, 1);
	gicv3_wait_for_rwp(is_spi ? DIST_BIT : cpu);
}

static void gicv3_irq_set_active(uint32_t intid)
{
	gicv3_write_reg(intid, GICD_ISACTIVER, 32, 1, 1);
}

static void gicv3_irq_clear_active(uint32_t intid)
{
	gicv3_write_reg(intid, GICD_ICACTIVER, 32, 1, 1);
}

static bool gicv3_irq_get_active(uint32_t intid)
{
	return gicv3_read_reg(intid, GICD_ISACTIVER, 32, 1);
}

static void gicv3_irq_set_pending(uint32_t intid)
{
	gicv3_write_reg(intid, GICD_ISPENDR, 32, 1, 1);
}

static void gicv3_irq_clear_pending(uint32_t intid)
{
	gicv3_write_reg(intid, GICD_ICPENDR, 32, 1, 1);
}

static bool gicv3_irq_get_pending(uint32_t intid)
{
	return gicv3_read_reg(intid, GICD_ISPENDR, 32, 1);
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
	.gic_write_dir = gicv3_write_dir,
	.gic_set_priority_mask = gicv3_set_priority_mask,
	.gic_set_eoi_split = gicv3_set_eoi_split,
	.gic_set_priority = gicv3_set_priority,
	.gic_irq_set_active = gicv3_irq_set_active,
	.gic_irq_clear_active = gicv3_irq_clear_active,
	.gic_irq_get_active = gicv3_irq_get_active,
	.gic_irq_set_pending = gicv3_irq_set_pending,
	.gic_irq_clear_pending = gicv3_irq_clear_pending,
	.gic_irq_get_pending = gicv3_irq_get_pending,
	.gic_irq_set_config = gicv3_irq_set_config,
};
