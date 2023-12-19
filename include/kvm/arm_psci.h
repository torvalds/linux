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
	if (vcpu_has_feature(vcpu, KVM_ARM_VCPU_PSCI_0_2)) {
		if (vcpu->kvm->arch.psci_version)
			return vcpu->kvm->arch.psci_version;

		return KVM_ARM_PSCI_LATEST;
	}

	return KVM_ARM_PSCI_0_1;
}


int kvm_psci_call(struct kvm_vcpu *vcpu);

#endif /* __KVM_ARM_PSCI_H__ */
