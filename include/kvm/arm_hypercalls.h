/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 Arm Ltd. */

#ifndef __KVM_ARM_HYPERCALLS_H
#define __KVM_ARM_HYPERCALLS_H

#include <asm/kvm_emulate.h>

int kvm_hvc_call_handler(struct kvm_vcpu *vcpu);

static inline u32 smccc_get_function(struct kvm_vcpu *vcpu)
{
	return vcpu_get_reg(vcpu, 0);
}

static inline unsigned long smccc_get_arg1(struct kvm_vcpu *vcpu)
{
	return vcpu_get_reg(vcpu, 1);
}

static inline unsigned long smccc_get_arg2(struct kvm_vcpu *vcpu)
{
	return vcpu_get_reg(vcpu, 2);
}

static inline unsigned long smccc_get_arg3(struct kvm_vcpu *vcpu)
{
	return vcpu_get_reg(vcpu, 3);
}

static inline void smccc_set_retval(struct kvm_vcpu *vcpu,
				    unsigned long a0,
				    unsigned long a1,
				    unsigned long a2,
				    unsigned long a3)
{
	vcpu_set_reg(vcpu, 0, a0);
	vcpu_set_reg(vcpu, 1, a1);
	vcpu_set_reg(vcpu, 2, a2);
	vcpu_set_reg(vcpu, 3, a3);
}

struct kvm_one_reg;

void kvm_arm_init_hypercalls(struct kvm *kvm);
int kvm_arm_get_fw_num_regs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_fw_reg_indices(struct kvm_vcpu *vcpu, u64 __user *uindices);
int kvm_arm_get_fw_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
int kvm_arm_set_fw_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);

#endif
