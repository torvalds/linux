// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Generic Interrupt Controller (GIC) v3 support
 */

#include <linux/sizes.h>

#include "kvm_util.h"
#include "processor.h"
#include "delay.h"

#include "gic.h"
#include "gic_v3.h"
#include "gic_private.h"

#define GICV3_MAX_CPUS			512

#define GICD_INT_DEF_PRI		0xa0
#define GICD_INT_DEF_PRI_X4		((GICD_INT_DEF_PRI << 24) |\
					(GICD_INT_DEF_PRI << 16) |\
					(GICD_INT_DEF_PRI << 8) |\
					GICD_INT_DEF_PRI)

#define ICC_PMR_DEF_PRIO		0xf0

struct gicv3_data {
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

	while (readl(GICD_BASE_GVA + GICD_CTLR) & GICD_CTLR_RWP) {
		GUEST_ASSERT(count--);
		udelay(10);
	}
}

static inline volatile void *gicr_base_cpu(uint32_t cpu)
{
	/* Align all the redistributors sequentially */
	return GICR_BASE_GVA + cpu * SZ_64K * 2;
}

static void gicv3_gicr_wait_for_rwp(uint32_t cpu)
{
	unsigned int count = 100000; /* 1s */

	while (readl(gicr_base_cpu(cpu) + GICR_CTLR) & GICR_CTLR_RWP) {
		GUEST_ASSERT(count--);
		udelay(10);
	}
}

static void gicv3_wait_for_rwp(uint32_t cpu_or_dist)
{
	if (cpu_or_dist & DIST_BIT)
		gicv3_gicd_wait_for_rwp();
	else
		gicv3_gicr_wait_for_rwp(cpu_or_dist);
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
	volatile void *base = cpu_or_dist & DIST_BIT ? GICD_BASE_GVA
			: sgi_base_from_redist(gicr_base_cpu(cpu_or_dist));
	return readl(base + offset);
}

void gicv3_reg_writel(uint32_t cpu_or_dist, uint64_t offset, uint32_t reg_val)
{
	volatile void *base = cpu_or_dist & DIST_BIT ? GICD_BASE_GVA
			: sgi_base_from_redist(gicr_base_cpu(cpu_or_dist));
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

static void gicv3_enable_redist(volatile void *redist_base)
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

static void gicv3_cpu_init(unsigned int cpu)
{
	volatile void *sgi_base;
	unsigned int i;
	volatile void *redist_base_cpu;

	GUEST_ASSERT(cpu < gicv3_data.nr_cpus);

	redist_base_cpu = gicr_base_cpu(cpu);
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

	gicv3_gicr_wait_for_rwp(cpu);

	/* Enable the GIC system register (ICC_*) access */
	write_sysreg_s(read_sysreg_s(SYS_ICC_SRE_EL1) | ICC_SRE_EL1_SRE,
			SYS_ICC_SRE_EL1);

	/* Set a default priority threshold */
	write_sysreg_s(ICC_PMR_DEF_PRIO, SYS_ICC_PMR_EL1);

	/* Enable non-secure Group-1 interrupts */
	write_sysreg_s(ICC_IGRPEN1_EL1_MASK, SYS_ICC_IGRPEN1_EL1);
}

static void gicv3_dist_init(void)
{
	unsigned int i;

	/* Disable the distributor until we set things up */
	writel(0, GICD_BASE_GVA + GICD_CTLR);
	gicv3_gicd_wait_for_rwp();

	/*
	 * Mark all the SPI interrupts as non-secure Group-1.
	 * Also, deactivate and disable them.
	 */
	for (i = 32; i < gicv3_data.nr_spis; i += 32) {
		writel(~0, GICD_BASE_GVA + GICD_IGROUPR + i / 8);
		writel(~0, GICD_BASE_GVA + GICD_ICACTIVER + i / 8);
		writel(~0, GICD_BASE_GVA + GICD_ICENABLER + i / 8);
	}

	/* Set a default priority for all the SPIs */
	for (i = 32; i < gicv3_data.nr_spis; i += 4)
		writel(GICD_INT_DEF_PRI_X4,
				GICD_BASE_GVA + GICD_IPRIORITYR + i);

	/* Wait for the settings to sync-in */
	gicv3_gicd_wait_for_rwp();

	/* Finally, enable the distributor globally with ARE */
	writel(GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1A |
			GICD_CTLR_ENABLE_G1, GICD_BASE_GVA + GICD_CTLR);
	gicv3_gicd_wait_for_rwp();
}

static void gicv3_init(unsigned int nr_cpus)
{
	GUEST_ASSERT(nr_cpus <= GICV3_MAX_CPUS);

	gicv3_data.nr_cpus = nr_cpus;
	gicv3_data.nr_spis = GICD_TYPER_SPIS(
				readl(GICD_BASE_GVA + GICD_TYPER));
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

void gic_rdist_enable_lpis(vm_paddr_t cfg_table, size_t cfg_table_size,
			   vm_paddr_t pend_table)
{
	volatile void *rdist_base = gicr_base_cpu(guest_get_vcpuid());

	u32 ctlr;
	u64 val;

	val = (cfg_table |
	       GICR_PROPBASER_InnerShareable |
	       GICR_PROPBASER_RaWaWb |
	       ((ilog2(cfg_table_size) - 1) & GICR_PROPBASER_IDBITS_MASK));
	writeq_relaxed(val, rdist_base + GICR_PROPBASER);

	val = (pend_table |
	       GICR_PENDBASER_InnerShareable |
	       GICR_PENDBASER_RaWaWb);
	writeq_relaxed(val, rdist_base + GICR_PENDBASER);

	ctlr = readl_relaxed(rdist_base + GICR_CTLR);
	ctlr |= GICR_CTLR_ENABLE_LPIS;
	writel_relaxed(ctlr, rdist_base + GICR_CTLR);
}
