// SPDX-License-Identifier: GPL-2.0
/*
 * ucall support. A ucall is a "hypercall to userspace".
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */
#include "kvm_util.h"

#define UCALL_PIO_PORT ((uint16_t)0x1000)

void ucall_init(struct kvm_vm *vm, void *arg)
{
}

void ucall_uninit(struct kvm_vm *vm)
{
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

	asm volatile("in %[port], %%al"
		: : [port] "d" (UCALL_PIO_PORT), "D" (&uc) : "rax", "memory");
}

uint64_t get_ucall(struct kvm_vm *vm, uint32_t vcpu_id, struct ucall *uc)
{
	struct kvm_run *run = vcpu_state(vm, vcpu_id);
	struct ucall ucall = {};

	if (run->exit_reason == KVM_EXIT_IO && run->io.port == UCALL_PIO_PORT) {
		struct kvm_regs regs;

		vcpu_regs_get(vm, vcpu_id, &regs);
		memcpy(&ucall, addr_gva2hva(vm, (vm_vaddr_t)regs.rdi),
		       sizeof(ucall));

		vcpu_run_complete_io(vm, vcpu_id);
		if (uc)
			memcpy(uc, &ucall, sizeof(ucall));
	}

	return ucall.cmd;
}
