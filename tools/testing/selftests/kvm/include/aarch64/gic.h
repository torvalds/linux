/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM Generic Interrupt Controller (GIC) specific defines
 */

#ifndef SELFTEST_KVM_GIC_H
#define SELFTEST_KVM_GIC_H

enum gic_type {
	GIC_V3,
	GIC_TYPE_MAX,
};

void gic_init(enum gic_type type, unsigned int nr_cpus,
		void *dist_base, void *redist_base);
void gic_irq_enable(unsigned int intid);
void gic_irq_disable(unsigned int intid);
unsigned int gic_get_and_ack_irq(void);
void gic_set_eoi(unsigned int intid);

#endif /* SELFTEST_KVM_GIC_H */
