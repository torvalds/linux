// SPDX-License-Identifier: GPL-2.0
/*
 * ucall support. A ucall is a "hypercall to userspace".
 *
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/kvm.h>

#include "kvm_util.h"
#include "processor.h"

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
			unsigned long arg1, unsigned long arg2,
			unsigned long arg3, unsigned long arg4,
			unsigned long arg5)
{
	register uintptr_t a0 asm ("a0") = (uintptr_t)(arg0);
	register uintptr_t a1 asm ("a1") = (uintptr_t)(arg1);
	register uintptr_t a2 asm ("a2") = (uintptr_t)(arg2);
	register uintptr_t a3 asm ("a3") = (uintptr_t)(arg3);
	register uintptr_t a4 asm ("a4") = (uintptr_t)(arg4);
	register uintptr_t a5 asm ("a5") = (uintptr_t)(arg5);
	register uintptr_t a6 asm ("a6") = (uintptr_t)(fid);
	register uintptr_t a7 asm ("a7") = (uintptr_t)(ext);
	struct sbiret ret;

	asm volatile (
		"ecall"
		: "+r" (a0), "+r" (a1)
		: "r" (a2), "r" (a3), "r" (a4), "r" (a5), "r" (a6), "r" (a7)
		: "memory");
	ret.error = a0;
	ret.value = a1;

	return ret;
}

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
