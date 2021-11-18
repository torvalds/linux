/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM Generic Interrupt Controller (GIC) private defines that's only
 * shared among the GIC library code.
 */

#ifndef SELFTEST_KVM_GIC_PRIVATE_H
#define SELFTEST_KVM_GIC_PRIVATE_H

struct gic_common_ops {
	void (*gic_init)(unsigned int nr_cpus, void *dist_base);
	void (*gic_cpu_init)(unsigned int cpu, void *redist_base);
	void (*gic_irq_enable)(unsigned int intid);
	void (*gic_irq_disable)(unsigned int intid);
	uint64_t (*gic_read_iar)(void);
	void (*gic_write_eoir)(uint32_t irq);
};

extern const struct gic_common_ops gicv3_ops;

#endif /* SELFTEST_KVM_GIC_PRIVATE_H */
