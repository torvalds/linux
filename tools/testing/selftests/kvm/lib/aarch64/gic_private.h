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
	void (*gic_write_dir)(uint32_t irq);
	void (*gic_set_eoi_split)(bool split);
	void (*gic_set_priority_mask)(uint64_t mask);
	void (*gic_set_priority)(uint32_t intid, uint32_t prio);
	void (*gic_irq_set_active)(uint32_t intid);
	void (*gic_irq_clear_active)(uint32_t intid);
	bool (*gic_irq_get_active)(uint32_t intid);
	void (*gic_irq_set_pending)(uint32_t intid);
	void (*gic_irq_clear_pending)(uint32_t intid);
	bool (*gic_irq_get_pending)(uint32_t intid);
	void (*gic_irq_set_config)(uint32_t intid, bool is_edge);
};

extern const struct gic_common_ops gicv3_ops;

#endif /* SELFTEST_KVM_GIC_PRIVATE_H */
