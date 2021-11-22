/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM Generic Interrupt Controller (GIC) host specific defines
 */

#ifndef SELFTEST_KVM_VGIC_H
#define SELFTEST_KVM_VGIC_H

#include <linux/kvm.h>

#define REDIST_REGION_ATTR_ADDR(count, base, flags, index) \
	(((uint64_t)(count) << 52) | \
	((uint64_t)((base) >> 16) << 16) | \
	((uint64_t)(flags) << 12) | \
	index)

int vgic_v3_setup(struct kvm_vm *vm, unsigned int nr_vcpus,
		uint64_t gicd_base_gpa, uint64_t gicr_base_gpa);

#endif /* SELFTEST_KVM_VGIC_H */
