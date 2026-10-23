// SPDX-License-Identifier: GPL-2.0
/*
 * ucall support. A ucall is a "hypercall to host userspace".
 */
#include "kvm_util.h"
#include "ucall_common.h"
#include "hcall.h"

void *ucall_arch_get_ucall(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	if (run->exit_reason == UCALL_EXIT_REASON &&
	    run->papr_hcall.nr == H_UCALL) {
		struct kvm_regs regs;

		vcpu_regs_get(vcpu, &regs);
		if (regs.gpr[4] == UCALL_R4_UCALL)
			return (void *)regs.gpr[5];
	}
	return NULL;
}
