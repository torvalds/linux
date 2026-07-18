/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM Generic Interrupt Controller (GIC) private defines that's only
 * shared among the GIC library code.
 */

#ifndef SELFTEST_KVM_GIC_PRIVATE_H
#define SELFTEST_KVM_GIC_PRIVATE_H

struct gic_common_ops {
	void (*gic_init)(unsigned int nr_cpus);
	void (*gic_cpu_init)(unsigned int cpu);
	void (*gic_irq_enable)(unsigned int intid);
	void (*gic_irq_disable)(unsigned int intid);
	u64 (*gic_read_iar)(void);
	void (*gic_write_eoir)(u32 irq);
	void (*gic_write_dir)(u32 irq);
	void (*gic_set_eoi_split)(bool split);
	void (*gic_set_priority_mask)(u64 mask);
	void (*gic_set_priority)(u32 intid, u32 prio);
	void (*gic_irq_set_active)(u32 intid);
	void (*gic_irq_clear_active)(u32 intid);
	bool (*gic_irq_get_active)(u32 intid);
	void (*gic_irq_set_pending)(u32 intid);
	void (*gic_irq_clear_pending)(u32 intid);
	bool (*gic_irq_get_pending)(u32 intid);
	void (*gic_irq_set_config)(u32 intid, bool is_edge);
	void (*gic_irq_set_group)(u32 intid, bool group);
};

extern const struct gic_common_ops gicv3_ops;

#endif /* SELFTEST_KVM_GIC_PRIVATE_H */
