/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __KVM_ARM_PSCI_H__
#define __KVM_ARM_PSCI_H__

#include <linux/kvm_host.h>
#include <uapi/linux/psci.h>

#define KVM_ARM_PSCI_0_1	PSCI_VERSION(0, 1)
#define KVM_ARM_PSCI_0_2	PSCI_VERSION(0, 2)
#define KVM_ARM_PSCI_1_0	PSCI_VERSION(1, 0)

#define KVM_ARM_PSCI_LATEST	KVM_ARM_PSCI_1_0

/*
 * We need the KVM pointer independently from the vcpu as we can call
 * this from HYP, and need to apply kern_hyp_va on it...
 */
static inline int kvm_psci_version(struct kvm_vcpu *vcpu, struct kvm *kvm)
{
	/*
	 * Our PSCI implementation stays the same across versions from
	 * v0.2 onward, only adding the few mandatory functions (such
	 * as FEATURES with 1.0) that are required by newer
	 * revisions. It is thus safe to return the latest.
	 */
	if (test_bit(KVM_ARM_VCPU_PSCI_0_2, vcpu->arch.features))
		return KVM_ARM_PSCI_LATEST;

	return KVM_ARM_PSCI_0_1;
}


int kvm_hvc_call_handler(struct kvm_vcpu *vcpu);

#endif /* __KVM_ARM_PSCI_H__ */
