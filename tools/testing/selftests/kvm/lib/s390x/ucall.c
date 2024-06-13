// SPDX-License-Identifier: GPL-2.0
/*
 * ucall support. A ucall is a "hypercall to userspace".
 *
 * Copyright (C) 2019 Red Hat, Inc.
 */
#include "kvm_util.h"

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

	nargs = min(nargs, UCALL_MAX_ARGS);

	va_start(va, nargs);
	for (i = 0; i < nargs; ++i)
		uc.args[i] = va_arg(va, uint64_t);
	va_end(va);

	/* Exit via DIAGNOSE 0x501 (normally used for breakpoints) */
	asm volatile ("diag 0,%0,0x501" : : "a"(&uc) : "memory");
}

uint64_t get_ucall(struct kvm_vcpu *vcpu, struct ucall *uc)
{
	struct kvm_run *run = vcpu->run;
	struct ucall ucall = {};

	if (uc)
		memset(uc, 0, sizeof(*uc));

	if (run->exit_reason == KVM_EXIT_S390_SIEIC &&
	    run->s390_sieic.icptcode == 4 &&
	    (run->s390_sieic.ipa >> 8) == 0x83 &&    /* 0x83 means DIAGNOSE */
	    (run->s390_sieic.ipb >> 16) == 0x501) {
		int reg = run->s390_sieic.ipa & 0xf;

		memcpy(&ucall, addr_gva2hva(vcpu->vm, run->s.regs.gprs[reg]),
		       sizeof(ucall));

		vcpu_run_complete_io(vcpu);
		if (uc)
			memcpy(uc, &ucall, sizeof(ucall));
	}

	return ucall.cmd;
}
