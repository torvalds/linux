/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __KVM_ARM_PSCI_H__
#define __KVM_ARM_PSCI_H__

#include <linux/kvm_host.h>
#include <uapi/linux/psci.h>

#define KVM_ARM_PSCI_0_1	PSCI_VERSION(0, 1)
#define KVM_ARM_PSCI_0_2	PSCI_VERSION(0, 2)
#define KVM_ARM_PSCI_1_0	PSCI_VERSION(1, 0)
#define KVM_ARM_PSCI_1_1	PSCI_VERSION(1, 1)

#define KVM_ARM_PSCI_LATEST	KVM_ARM_PSCI_1_1

static inline int kvm_psci_version(struct kvm_vcpu *vcpu)
{
	/*
	 * Our PSCI implementation stays the same across versions from
	 * v0.2 onward, only adding the few mandatory functions (such
	 * as FEATURES with 1.0) that are required by newer
	 * revisions. It is thus safe to return the latest, unless
	 * userspace has instructed us otherwise.
	 */
	if (test_bit(KVM_ARM_VCPU_PSCI_0_2, vcpu->arch.features)) {
		if (vcpu->kvm->arch.psci_version)
			return vcpu->kvm->arch.psci_version;

		return KVM_ARM_PSCI_LATEST;
	}

	return KVM_ARM_PSCI_0_1;
}

/* Narrow the PSCI register arguments (r1 to r3) to 32 bits. */
static inline void kvm_psci_narrow_to_32bit(struct kvm_vcpu *vcpu)
{
	int i;

	/*
	 * Zero the input registers' upper 32 bits. They will be fully
	 * zeroed on exit, so we're fine changing them in place.
	 */
	for (i = 1; i < 4; i++)
		vcpu_set_reg(vcpu, i, lower_32_bits(vcpu_get_reg(vcpu, i)));
}

static inline bool kvm_psci_valid_affinity(struct kvm_vcpu *vcpu,
					   unsigned long affinity)
{
	return !(affinity & ~MPIDR_HWID_BITMASK);
}


#define AFFINITY_MASK(level)	~((0x1UL << ((level) * MPIDR_LEVEL_BITS)) - 1)

static inline unsigned long psci_affinity_mask(unsigned long affinity_level)
{
	if (affinity_level <= 3)
		return MPIDR_HWID_BITMASK & AFFINITY_MASK(affinity_level);

	return 0;
}

int kvm_psci_call(struct kvm_vcpu *vcpu);

#endif /* __KVM_ARM_PSCI_H__ */
