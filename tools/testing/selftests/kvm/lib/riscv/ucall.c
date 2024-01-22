// SPDX-License-Identifier: GPL-2.0
/*
 * ucall support. A ucall is a "hypercall to userspace".
 *
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/kvm.h>

#include "kvm_util.h"
#include "processor.h"

void *ucall_arch_get_ucall(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	if (run->exit_reason == KVM_EXIT_RISCV_SBI &&
	    run->riscv_sbi.extension_id == KVM_RISCV_SELFTESTS_SBI_EXT) {
		switch (run->riscv_sbi.function_id) {
		case KVM_RISCV_SELFTESTS_SBI_UCALL:
			return (void *)run->riscv_sbi.args[0];
		case KVM_RISCV_SELFTESTS_SBI_UNEXP:
			vcpu_dump(stderr, vcpu, 2);
			TEST_ASSERT(0, "Unexpected trap taken by guest");
			break;
		default:
			break;
		}
	}
	return NULL;
}
