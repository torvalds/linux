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
#include "apic.h"

#define DR6_BD		(1 << 13)
#define DR7_GD		(1 << 13)

#define IRQ_VECTOR 0xAA

#define  CAST_TO_RIP(v)  ((unsigned long long)&(v))

/* For testing data access debug BP */
u32 guest_value;

extern unsigned char sw_bp, hw_bp, write_data, ss_start, bd_start;
extern unsigned char fep_bd_start, fep_sti_start, fep_sti_end;

static int irqs_received;

static void guest_db_handler(struct ex_regs *regs)
{
	static int count;
	unsigned long target_rips[2] = {
		CAST_TO_RIP(fep_sti_start),
		CAST_TO_RIP(fep_sti_end),
	};

	__GUEST_ASSERT(regs->rip == target_rips[count],
	               "STI[%u]: unexpected rip 0x%lx (should be 0x%lx)",
		       count, regs->rip, target_rips[count]);
	regs->rflags &= ~X86_EFLAGS_TF;
	count++;
}

static void guest_irq_handler(struct ex_regs *regs)
{
	/*
	 * The pending IRQ should finally be take when KVM_GUESTDBG_BLOCKIRQ is
	 * cleared and IRQs are enabled.  Note, the IRQ is expected to arrive
	 * on the instruction immediately after STI, even though its in an STI
	 * shadow.  Because the next instruction has a coincident #DB, and #DBs
	 * are not subject to STI-blocking, the #DB will push RFLAGS.IF=1 on
	 * the stack, and the eventual IRET will unmask IRQs and obliterate the
	 * STI shadow in the process.
	 */
	unsigned long target_rip = CAST_TO_RIP(fep_sti_start);

	__GUEST_ASSERT(regs->rip == target_rip,
		       "IRQ: unexpected rip 0x%lx (should be 0x%lx)",
		       regs->rip, target_rip);

	irqs_received++;
	x2apic_write_reg(APIC_EOI, 0);
}

static void guest_code(void)
{
	/* Create a pending interrupt on current vCPU */
	x2apic_enable();
	x2apic_write_reg(APIC_ICR, APIC_DEST_SELF | APIC_INT_ASSERT |
			 APIC_DM_FIXED | IRQ_VECTOR);

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

	/*
	 * Single step test, covers 2 basic instructions and 2 emulated
	 *
	 * Enable interrupts during the single stepping to see that pending
	 * interrupt we raised is not handled due to KVM_GUESTDBG_BLOCKIRQ.
	 *
	 * Write MSR_IA32_TSC_DEADLINE to verify that KVM's fastpath handler
	 * exits to userspace due to single-step being enabled.
	 */
	asm volatile("ss_start: "
		     "sti\n\t"
		     "xor %%eax,%%eax\n\t"
		     "cpuid\n\t"
		     "movl $" __stringify(MSR_IA32_TSC_DEADLINE) ", %%ecx\n\t"
		     "wrmsr\n\t"
		     "cli\n\t"
		     : : : "eax", "ebx", "ecx", "edx");

	/* DR6.BD test */
	asm volatile("bd_start: mov %%dr0, %%rax" : : : "rax");

	/*
	 * Note, the IRET from the #DB that occurs in the below STI-shadow will
	 * unmask IRQs, i.e. the pending interrupt will be delivered after #DB
	 * handling, on the CLI!
	 */
	if (is_forced_emulation_enabled) {
		asm volatile(KVM_FEP "fep_bd_start: mov %%dr0, %%rax" : : : "rax");

		/* pending debug exceptions for emulation */
		asm volatile("pushf\n\t"
			     "orq $" __stringify(X86_EFLAGS_TF) ", (%rsp)\n\t"
			     "popf\n\t"
			     "sti\n\t"
			     "fep_sti_start:"
			     "cli\n\t"
			     "pushf\n\t"
			     "orq $" __stringify(X86_EFLAGS_TF) ", (%rsp)\n\t"
			     "popf\n\t"
			     KVM_FEP "sti\n\t"
			     "fep_sti_end:"
			     "cli\n\t");
		GUEST_ASSERT(irqs_received == 1);
	}
	GUEST_DONE();
}

static void vcpu_skip_insn(struct kvm_vcpu *vcpu, int insn_len)
{
	struct kvm_regs regs;

	vcpu_regs_get(vcpu, &regs);
	regs.rip += insn_len;
	vcpu_regs_set(vcpu, &regs);
}

int main(void)
{
	struct kvm_guest_debug debug;
	unsigned long long target_dr6, target_rip;
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct ucall uc;
	u64 cmd;
	int i;
	/* Instruction lengths starting at ss_start */
	int ss_size[6] = {
		1,		/* sti*/
		2,		/* xor */
		2,		/* cpuid */
		5,		/* mov */
		2,		/* rdmsr */
		1,		/* cli */
	};

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_SET_GUEST_DEBUG));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	run = vcpu->run;

	/* Test software BPs - int3 */
	memset(&debug, 0, sizeof(debug));
	debug.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP;
	vcpu_guest_debug_set(vcpu, &debug);
	vcpu_run(vcpu);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_DEBUG &&
		    run->debug.arch.exception == BP_VECTOR &&
		    run->debug.arch.pc == CAST_TO_RIP(sw_bp),
		    "INT3: exit %d exception %d rip 0x%llx (should be 0x%llx)",
		    run->exit_reason, run->debug.arch.exception,
		    run->debug.arch.pc, CAST_TO_RIP(sw_bp));
	vcpu_skip_insn(vcpu, 1);

	/* Test instruction HW BP over DR[0-3] */
	for (i = 0; i < 4; i++) {
		memset(&debug, 0, sizeof(debug));
		debug.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW_BP;
		debug.arch.debugreg[i] = CAST_TO_RIP(hw_bp);
		debug.arch.debugreg[7] = 0x400 | (1UL << (2*i+1));
		vcpu_guest_debug_set(vcpu, &debug);
		vcpu_run(vcpu);
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
	vcpu_skip_insn(vcpu, 1);

	/* Test data access HW BP over DR[0-3] */
	for (i = 0; i < 4; i++) {
		memset(&debug, 0, sizeof(debug));
		debug.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW_BP;
		debug.arch.debugreg[i] = CAST_TO_RIP(guest_value);
		debug.arch.debugreg[7] = 0x00000400 | (1UL << (2*i+1)) |
		    (0x000d0000UL << (4*i));
		vcpu_guest_debug_set(vcpu, &debug);
		vcpu_run(vcpu);
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
		vcpu_skip_insn(vcpu, -7);
	}
	/* Skip the 4-bytes "mov" */
	vcpu_skip_insn(vcpu, 7);

	/* Test single step */
	target_rip = CAST_TO_RIP(ss_start);
	target_dr6 = 0xffff4ff0ULL;
	for (i = 0; i < ARRAY_SIZE(ss_size); i++) {
		target_rip += ss_size[i];
		memset(&debug, 0, sizeof(debug));
		debug.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP |
				KVM_GUESTDBG_BLOCKIRQ;
		debug.arch.debugreg[7] = 0x00000400;
		vcpu_guest_debug_set(vcpu, &debug);
		vcpu_run(vcpu);
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

	/* test global disable */
	memset(&debug, 0, sizeof(debug));
	debug.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW_BP;
	debug.arch.debugreg[7] = 0x400 | DR7_GD;
	vcpu_guest_debug_set(vcpu, &debug);
	vcpu_run(vcpu);
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

	/* test global disable in emulation */
	if (is_forced_emulation_enabled) {
		/* Skip the 3-bytes "mov dr0" */
		vcpu_skip_insn(vcpu, 3);
		vcpu_run(vcpu);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_DEBUG &&
			    run->debug.arch.exception == DB_VECTOR &&
			    run->debug.arch.pc == CAST_TO_RIP(fep_bd_start) &&
			    run->debug.arch.dr6 == target_dr6,
			    "DR7.GD: exit %d exception %d rip 0x%llx "
			    "(should be 0x%llx) dr6 0x%llx (should be 0x%llx)",
			    run->exit_reason, run->debug.arch.exception,
			    run->debug.arch.pc, CAST_TO_RIP(fep_bd_start),
			    run->debug.arch.dr6, target_dr6);
	}

	/* Disable all debug controls, run to the end */
	memset(&debug, 0, sizeof(debug));
	vcpu_guest_debug_set(vcpu, &debug);

	vm_install_exception_handler(vm, DB_VECTOR, guest_db_handler);
	vm_install_exception_handler(vm, IRQ_VECTOR, guest_irq_handler);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
	cmd = get_ucall(vcpu, &uc);
	TEST_ASSERT(cmd == UCALL_DONE, "UCALL_DONE");

	kvm_vm_free(vm);

	return 0;
}
