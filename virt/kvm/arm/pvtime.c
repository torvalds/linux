// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Arm Ltd.

#include <linux/arm-smccc.h>

#include <asm/pvclock-abi.h>

#include <kvm/arm_hypercalls.h>

void kvm_update_stolen_time(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	u64 steal;
	__le64 steal_le;
	u64 offset;
	int idx;
	u64 base = vcpu->arch.steal.base;

	if (base == GPA_INVALID)
		return;

	/* Let's do the local bookkeeping */
	steal = vcpu->arch.steal.steal;
	steal += current->sched_info.run_delay - vcpu->arch.steal.last_steal;
	vcpu->arch.steal.last_steal = current->sched_info.run_delay;
	vcpu->arch.steal.steal = steal;

	steal_le = cpu_to_le64(steal);
	idx = srcu_read_lock(&kvm->srcu);
	offset = offsetof(struct pvclock_vcpu_stolen_time, stolen_time);
	kvm_put_guest(kvm, base + offset, steal_le, u64);
	srcu_read_unlock(&kvm->srcu, idx);
}

long kvm_hypercall_pv_features(struct kvm_vcpu *vcpu)
{
	u32 feature = smccc_get_arg1(vcpu);
	long val = SMCCC_RET_NOT_SUPPORTED;

	switch (feature) {
	case ARM_SMCCC_HV_PV_TIME_FEATURES:
	case ARM_SMCCC_HV_PV_TIME_ST:
		val = SMCCC_RET_SUCCESS;
		break;
	}

	return val;
}

gpa_t kvm_init_stolen_time(struct kvm_vcpu *vcpu)
{
	struct pvclock_vcpu_stolen_time init_values = {};
	struct kvm *kvm = vcpu->kvm;
	u64 base = vcpu->arch.steal.base;
	int idx;

	if (base == GPA_INVALID)
		return base;

	/*
	 * Start counting stolen time from the time the guest requests
	 * the feature enabled.
	 */
	vcpu->arch.steal.steal = 0;
	vcpu->arch.steal.last_steal = current->sched_info.run_delay;

	idx = srcu_read_lock(&kvm->srcu);
	kvm_write_guest(kvm, base, &init_values, sizeof(init_values));
	srcu_read_unlock(&kvm->srcu, idx);

	return base;
}
