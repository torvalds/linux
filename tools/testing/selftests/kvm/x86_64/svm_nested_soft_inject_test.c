// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oracle and/or its affiliates.
 *
 * Based on:
 *   svm_int_ctl_test
 *
 *   Copyright (C) 2021, Red Hat, Inc.
 *
 */

#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>
#include "apic.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "test_util.h"

#define INT_NR			0x20

static_assert(ATOMIC_INT_LOCK_FREE == 2, "atomic int is not lockless");

static unsigned int bp_fired;
static void guest_bp_handler(struct ex_regs *regs)
{
	bp_fired++;
}

static unsigned int int_fired;
static void l2_guest_code_int(void);

static void guest_int_handler(struct ex_regs *regs)
{
	int_fired++;
	GUEST_ASSERT_2(regs->rip == (unsigned long)l2_guest_code_int,
		       regs->rip, (unsigned long)l2_guest_code_int);
}

static void l2_guest_code_int(void)
{
	GUEST_ASSERT_1(int_fired == 1, int_fired);

	/*
         * Same as the vmmcall() function, but with a ud2 sneaked after the
         * vmmcall.  The caller injects an exception with the return address
         * increased by 2, so the "pop rbp" must be after the ud2 and we cannot
	 * use vmmcall() directly.
         */
	__asm__ __volatile__("push %%rbp; vmmcall; ud2; pop %%rbp"
                             : : "a"(0xdeadbeef), "c"(0xbeefdead)
                             : "rbx", "rdx", "rsi", "rdi", "r8", "r9",
                               "r10", "r11", "r12", "r13", "r14", "r15");

	GUEST_ASSERT_1(bp_fired == 1, bp_fired);
	hlt();
}

static atomic_int nmi_stage;
#define nmi_stage_get() atomic_load_explicit(&nmi_stage, memory_order_acquire)
#define nmi_stage_inc() atomic_fetch_add_explicit(&nmi_stage, 1, memory_order_acq_rel)
static void guest_nmi_handler(struct ex_regs *regs)
{
	nmi_stage_inc();

	if (nmi_stage_get() == 1) {
		vmmcall();
		GUEST_ASSERT(false);
	} else {
		GUEST_ASSERT_1(nmi_stage_get() == 3, nmi_stage_get());
		GUEST_DONE();
	}
}

static void l2_guest_code_nmi(void)
{
	ud2();
}

static void l1_guest_code(struct svm_test_data *svm, uint64_t is_nmi, uint64_t idt_alt)
{
	#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	struct vmcb *vmcb = svm->vmcb;

	if (is_nmi)
		x2apic_enable();

	/* Prepare for L2 execution. */
	generic_svm_setup(svm,
			  is_nmi ? l2_guest_code_nmi : l2_guest_code_int,
			  &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	vmcb->control.intercept_exceptions |= BIT(PF_VECTOR) | BIT(UD_VECTOR);
	vmcb->control.intercept |= BIT(INTERCEPT_NMI) | BIT(INTERCEPT_HLT);

	if (is_nmi) {
		vmcb->control.event_inj = SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_NMI;
	} else {
		vmcb->control.event_inj = INT_NR | SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_SOFT;
		/* The return address pushed on stack */
		vmcb->control.next_rip = vmcb->save.rip;
	}

	run_guest(vmcb, svm->vmcb_gpa);
	GUEST_ASSERT_3(vmcb->control.exit_code == SVM_EXIT_VMMCALL,
		       vmcb->control.exit_code,
		       vmcb->control.exit_info_1, vmcb->control.exit_info_2);

	if (is_nmi) {
		clgi();
		x2apic_write_reg(APIC_ICR, APIC_DEST_SELF | APIC_INT_ASSERT | APIC_DM_NMI);

		GUEST_ASSERT_1(nmi_stage_get() == 1, nmi_stage_get());
		nmi_stage_inc();

		stgi();
		/* self-NMI happens here */
		while (true)
			cpu_relax();
	}

	/* Skip over VMMCALL */
	vmcb->save.rip += 3;

	/* Switch to alternate IDT to cause intervening NPF again */
	vmcb->save.idtr.base = idt_alt;
	vmcb->control.clean = 0; /* &= ~BIT(VMCB_DT) would be enough */

	vmcb->control.event_inj = BP_VECTOR | SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_EXEPT;
	/* The return address pushed on stack, skip over UD2 */
	vmcb->control.next_rip = vmcb->save.rip + 2;

	run_guest(vmcb, svm->vmcb_gpa);
	GUEST_ASSERT_3(vmcb->control.exit_code == SVM_EXIT_HLT,
		       vmcb->control.exit_code,
		       vmcb->control.exit_info_1, vmcb->control.exit_info_2);

	GUEST_DONE();
}

static void run_test(bool is_nmi)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	vm_vaddr_t svm_gva;
	vm_vaddr_t idt_alt_vm;
	struct kvm_guest_debug debug;

	pr_info("Running %s test\n", is_nmi ? "NMI" : "soft int");

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);

	vm_install_exception_handler(vm, NMI_VECTOR, guest_nmi_handler);
	vm_install_exception_handler(vm, BP_VECTOR, guest_bp_handler);
	vm_install_exception_handler(vm, INT_NR, guest_int_handler);

	vcpu_alloc_svm(vm, &svm_gva);

	if (!is_nmi) {
		void *idt, *idt_alt;

		idt_alt_vm = vm_vaddr_alloc_page(vm);
		idt_alt = addr_gva2hva(vm, idt_alt_vm);
		idt = addr_gva2hva(vm, vm->idt);
		memcpy(idt_alt, idt, getpagesize());
	} else {
		idt_alt_vm = 0;
	}
	vcpu_args_set(vcpu, 3, svm_gva, (uint64_t)is_nmi, (uint64_t)idt_alt_vm);

	memset(&debug, 0, sizeof(debug));
	vcpu_guest_debug_set(vcpu, &debug);

	struct kvm_run *run = vcpu->run;
	struct ucall uc;

	alarm(2);
	vcpu_run(vcpu);
	alarm(0);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Got exit_reason other than KVM_EXIT_IO: %u (%s)\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT_3(uc, "vals = 0x%lx 0x%lx 0x%lx");
		break;
		/* NOT REACHED */
	case UCALL_DONE:
		goto done;
	default:
		TEST_FAIL("Unknown ucall 0x%lx.", uc.cmd);
	}
done:
	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM));

	TEST_ASSERT(kvm_cpu_has(X86_FEATURE_NRIPS),
		    "KVM with nSVM is supposed to unconditionally advertise nRIP Save");

	atomic_init(&nmi_stage, 0);

	run_test(false);
	run_test(true);

	return 0;
}
