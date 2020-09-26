// SPDX-License-Identifier: GPL-2.0
/*
 * KVM guest debug register tests
 *
 * Copyright (C) 2020, Red Hat, Inc.
 */
#include <stdio.h>
#include <string.h>
#include "kvm_util.h"
#include "processor.h"

#define VCPU_ID 0

#define DR6_BD		(1 << 13)
#define DR7_GD		(1 << 13)

/* For testing data access debug BP */
uint32_t guest_value;

extern unsigned char sw_bp, hw_bp, write_data, ss_start, bd_start;

static void guest_code(void)
{
	/*
	 * Software BP tests.
	 *
	 * NOTE: sw_bp need to be before the cmd here, because int3 is an
	 * exception rather than a normal trap for KVM_SET_GUEST_DEBUG (we
	 * capture it using the vcpu exception bitmap).
	 */
	asm volatile("sw_bp: int3");

	/* Hardware instruction BP test */
	asm volatile("hw_bp: nop");

	/* Hardware data BP test */
	asm volatile("mov $1234,%%rax;\n\t"
		     "mov %%rax,%0;\n\t write_data:"
		     : "=m" (guest_value) : : "rax");

	/* Single step test, covers 2 basic instructions and 2 emulated */
	asm volatile("ss_start: "
		     "xor %%eax,%%eax\n\t"
		     "cpuid\n\t"
		     "movl $0x1a0,%%ecx\n\t"
		     "rdmsr\n\t"
		     : : : "eax", "ebx", "ecx", "edx");

	/* DR6.BD test */
	asm volatile("bd_start: mov %%dr0, %%rax" : : : "rax");
	GUEST_DONE();
}

#define  CLEAR_DEBUG()  memset(&debug, 0, sizeof(debug))
#define  APPLY_DEBUG()  vcpu_set_guest_debug(vm, VCPU_ID, &debug)
#define  CAST_TO_RIP(v)  ((unsigned long long)&(v))
#define  SET_RIP(v)  do {				\
		vcpu_regs_get(vm, VCPU_ID, &regs);	\
		regs.rip = (v);				\
		vcpu_regs_set(vm, VCPU_ID, &regs);	\
	} while (0)
#define  MOVE_RIP(v)  SET_RIP(regs.rip + (v));

int main(void)
{
	struct kvm_guest_debug debug;
	unsigned long long target_dr6, target_rip;
	struct kvm_regs regs;
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct ucall uc;
	uint64_t cmd;
	int i;
	/* Instruction lengths starting at ss_start */
	int ss_size[4] = {
		2,		/* xor */
		2,		/* cpuid */
		5,		/* mov */
		2,		/* rdmsr */
	};

	if (!kvm_check_cap(KVM_CAP_SET_GUEST_DEBUG)) {
		print_skip("KVM_CAP_SET_GUEST_DEBUG not supported");
		return 0;
	}

	vm = vm_create_default(VCPU_ID, 0, guest_code);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());
	run = vcpu_state(vm, VCPU_ID);

	/* Test software BPs - int3 */
	CLEAR_DEBUG();
	debug.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP;
	APPLY_DEBUG();
	vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_DEBUG &&
		    run->debug.arch.exception == BP_VECTOR &&
		    run->debug.arch.pc == CAST_TO_RIP(sw_bp),
		    "INT3: exit %d exception %d rip 0x%llx (should be 0x%llx)",
		    run->exit_reason, run->debug.arch.exception,
		    run->debug.arch.pc, CAST_TO_RIP(sw_bp));
	MOVE_RIP(1);

	/* Test instruction HW BP over DR[0-3] */
	for (i = 0; i < 4; i++) {
		CLEAR_DEBUG();
		debug.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW_BP;
		debug.arch.debugreg[i] = CAST_TO_RIP(hw_bp);
		debug.arch.debugreg[7] = 0x400 | (1UL << (2*i+1));
		APPLY_DEBUG();
		vcpu_run(vm, VCPU_ID);
		target_dr6 = 0xffff0ff0 | (1UL << i);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_DEBUG &&
			    run->debug.arch.exception == DB_VECTOR &&
			    run->debug.arch.pc == CAST_TO_RIP(hw_bp) &&
			    run->debug.arch.dr6 == target_dr6,
			    "INS_HW_BP (DR%d): exit %d exception %d rip 0x%llx "
			    "(should be 0x%llx) dr6 0x%llx (should be 0x%llx)",
			    i, run->exit_reason, run->debug.arch.exception,
			    run->debug.arch.pc, CAST_TO_RIP(hw_bp),
			    run->debug.arch.dr6, target_dr6);
	}
	/* Skip "nop" */
	MOVE_RIP(1);

	/* Test data access HW BP over DR[0-3] */
	for (i = 0; i < 4; i++) {
		CLEAR_DEBUG();
		debug.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW_BP;
		debug.arch.debugreg[i] = CAST_TO_RIP(guest_value);
		debug.arch.debugreg[7] = 0x00000400 | (1UL << (2*i+1)) |
		    (0x000d0000UL << (4*i));
		APPLY_DEBUG();
		vcpu_run(vm, VCPU_ID);
		target_dr6 = 0xffff0ff0 | (1UL << i);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_DEBUG &&
			    run->debug.arch.exception == DB_VECTOR &&
			    run->debug.arch.pc == CAST_TO_RIP(write_data) &&
			    run->debug.arch.dr6 == target_dr6,
			    "DATA_HW_BP (DR%d): exit %d exception %d rip 0x%llx "
			    "(should be 0x%llx) dr6 0x%llx (should be 0x%llx)",
			    i, run->exit_reason, run->debug.arch.exception,
			    run->debug.arch.pc, CAST_TO_RIP(write_data),
			    run->debug.arch.dr6, target_dr6);
		/* Rollback the 4-bytes "mov" */
		MOVE_RIP(-7);
	}
	/* Skip the 4-bytes "mov" */
	MOVE_RIP(7);

	/* Test single step */
	target_rip = CAST_TO_RIP(ss_start);
	target_dr6 = 0xffff4ff0ULL;
	vcpu_regs_get(vm, VCPU_ID, &regs);
	for (i = 0; i < (sizeof(ss_size) / sizeof(ss_size[0])); i++) {
		target_rip += ss_size[i];
		CLEAR_DEBUG();
		debug.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP;
		debug.arch.debugreg[7] = 0x00000400;
		APPLY_DEBUG();
		vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_DEBUG &&
			    run->debug.arch.exception == DB_VECTOR &&
			    run->debug.arch.pc == target_rip &&
			    run->debug.arch.dr6 == target_dr6,
			    "SINGLE_STEP[%d]: exit %d exception %d rip 0x%llx "
			    "(should be 0x%llx) dr6 0x%llx (should be 0x%llx)",
			    i, run->exit_reason, run->debug.arch.exception,
			    run->debug.arch.pc, target_rip, run->debug.arch.dr6,
			    target_dr6);
	}

	/* Finally test global disable */
	CLEAR_DEBUG();
	debug.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW_BP;
	debug.arch.debugreg[7] = 0x400 | DR7_GD;
	APPLY_DEBUG();
	vcpu_run(vm, VCPU_ID);
	target_dr6 = 0xffff0ff0 | DR6_BD;
	TEST_ASSERT(run->exit_reason == KVM_EXIT_DEBUG &&
		    run->debug.arch.exception == DB_VECTOR &&
		    run->debug.arch.pc == CAST_TO_RIP(bd_start) &&
		    run->debug.arch.dr6 == target_dr6,
			    "DR7.GD: exit %d exception %d rip 0x%llx "
			    "(should be 0x%llx) dr6 0x%llx (should be 0x%llx)",
			    run->exit_reason, run->debug.arch.exception,
			    run->debug.arch.pc, target_rip, run->debug.arch.dr6,
			    target_dr6);

	/* Disable all debug controls, run to the end */
	CLEAR_DEBUG();
	APPLY_DEBUG();

	vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO, "KVM_EXIT_IO");
	cmd = get_ucall(vm, VCPU_ID, &uc);
	TEST_ASSERT(cmd == UCALL_DONE, "UCALL_DONE");

	kvm_vm_free(vm);

	return 0;
}
