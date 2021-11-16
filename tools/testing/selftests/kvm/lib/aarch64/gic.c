// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Generic Interrupt Controller (GIC) support
 */

#include <errno.h>
#include <linux/bits.h>
#include <linux/sizes.h>

#include "kvm_util.h"

#include <gic.h>
#include "gic_private.h"
#include "processor.h"
#include "spinlock.h"

static const struct gic_common_ops *gic_common_ops;
static struct spinlock gic_lock;

static void gic_cpu_init(unsigned int cpu, void *redist_base)
{
	gic_common_ops->gic_cpu_init(cpu, redist_base);
}

static void
gic_dist_init(enum gic_type type, unsigned int nr_cpus, void *dist_base)
{
	const struct gic_common_ops *gic_ops = NULL;

	spin_lock(&gic_lock);

	/* Distributor initialization is needed only once per VM */
	if (gic_common_ops) {
		spin_unlock(&gic_lock);
		return;
	}

	if (type == GIC_V3)
		gic_ops = &gicv3_ops;

	GUEST_ASSERT(gic_ops);

	gic_ops->gic_init(nr_cpus, dist_base);
	gic_common_ops = gic_ops;

	/* Make sure that the initialized data is visible to all the vCPUs */
	dsb(sy);

	spin_unlock(&gic_lock);
}

void gic_init(enum gic_type type, unsigned int nr_cpus,
		void *dist_base, void *redist_base)
{
	uint32_t cpu = guest_get_vcpuid();

	GUEST_ASSERT(type < GIC_TYPE_MAX);
	GUEST_ASSERT(dist_base);
	GUEST_ASSERT(redist_base);
	GUEST_ASSERT(nr_cpus);

	gic_dist_init(type, nr_cpus, dist_base);
	gic_cpu_init(cpu, redist_base);
}

void gic_irq_enable(unsigned int intid)
{
	GUEST_ASSERT(gic_common_ops);
	gic_common_ops->gic_irq_enable(intid);
}

void gic_irq_disable(unsigned int intid)
{
	GUEST_ASSERT(gic_common_ops);
	gic_common_ops->gic_irq_disable(intid);
}

unsigned int gic_get_and_ack_irq(void)
{
	uint64_t irqstat;
	unsigned int intid;

	GUEST_ASSERT(gic_common_ops);

	irqstat = gic_common_ops->gic_read_iar();
	intid = irqstat & GENMASK(23, 0);

	return intid;
}

void gic_set_eoi(unsigned int intid)
{
	GUEST_ASSERT(gic_common_ops);
	gic_common_ops->gic_write_eoir(intid);
}
