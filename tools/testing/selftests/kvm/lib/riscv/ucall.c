// SPDX-License-Identifier: GPL-2.0
/*
 * ucall support. A ucall is a "hypercall to userspace".
 *
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/kvm.h>

#include "kvm_util.h"
#include "../kvm_util_internal.h"
#include "processor.h"

void ucall_init(struct kvm_vm *vm, void *arg)
{
}

void ucall_uninit(struct kvm_vm *vm)
{
}

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

void ucall(uint64_t cmd, int nargs, ...)
{
	struct ucall uc = {
		.cmd = cmd,
	};
	va_list va;
	int i;

	nargs = nargs <= UCALL_MAX_ARGS ? nargs : UCALL_MAX_ARGS;

	va_start(va, nargs);
	for (i = 0; i < nargs; ++i)
		uc.args[i] = va_arg(va, uint64_t);
	va_end(va);

	sbi_ecall(KVM_RISCV_SELFTESTS_SBI_EXT,
		  KVM_RISCV_SELFTESTS_SBI_UCALL,
		  (vm_vaddr_t)&uc, 0, 0, 0, 0, 0);
}

uint64_t get_ucall(struct kvm_vm *vm, uint32_t vcpu_id, struct ucall *uc)
{
	struct kvm_run *run = vcpu_state(vm, vcpu_id);
	struct ucall ucall = {};

	if (uc)
		memset(uc, 0, sizeof(*uc));

	if (run->exit_reason == KVM_EXIT_RISCV_SBI &&
	    run->riscv_sbi.extension_id == KVM_RISCV_SELFTESTS_SBI_EXT) {
		switch (run->riscv_sbi.function_id) {
		case KVM_RISCV_SELFTESTS_SBI_UCALL:
			memcpy(&ucall, addr_gva2hva(vm,
			       run->riscv_sbi.args[0]), sizeof(ucall));

			vcpu_run_complete_io(vm, vcpu_id);
			if (uc)
				memcpy(uc, &ucall, sizeof(ucall));

			break;
		case KVM_RISCV_SELFTESTS_SBI_UNEXP:
			vcpu_dump(stderr, vm, vcpu_id, 2);
			TEST_ASSERT(0, "Unexpected trap taken by guest");
			break;
		default:
			break;
		}
	}

	return ucall.cmd;
}
