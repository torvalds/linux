/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * powerpc processor specific defines
 */
#ifndef SELFTEST_KVM_PROCESSOR_H
#define SELFTEST_KVM_PROCESSOR_H

#include <linux/compiler.h>
#include "ppc_asm.h"
#include "kvm_util_types.h"

extern unsigned char __interrupts_start[];
extern unsigned char __interrupts_end[];

struct kvm_vm;
struct kvm_vcpu;

struct ex_regs {
	u64	gprs[32];
	u64	nia;
	u64	msr;
	u64	cfar;
	u64	lr;
	u64	ctr;
	u64	xer;
	u32	cr;
	u32	trap;
	gva_t	gva; /* gva of this struct */
};

void vm_install_exception_handler(struct kvm_vm *vm, int vector,
			void (*handler)(struct ex_regs *));

static inline void cpu_relax(void)
{
	asm volatile("" ::: "memory");
}

#endif
