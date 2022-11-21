// SPDX-License-Identifier: GPL-2.0-only
/*
 * svm_int_ctl_test
 *
 * Copyright (C) 2021, Red Hat, Inc.
 *
 * Nested SVM testing: test simultaneous use of V_IRQ from L1 and L0.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "apic.h"

#define VCPU_ID		0

static struct kvm_vm *vm;

bool vintr_irq_called;
bool intr_irq_called;

#define VINTR_IRQ_NUMBER 0x20
#define INTR_IRQ_NUMBER 0x30

static void vintr_irq_handler(struct ex_regs *regs)
{
	vintr_irq_called = true;
}

static void intr_irq_handler(struct ex_regs *regs)
{
	x2apic_write_reg(APIC_EOI, 0x00);
	intr_irq_called = true;
}

static void l2_guest_code(struct svm_test_data *svm)
{
	/* This code raises interrupt INTR_IRQ_NUMBER in the L1's LAPIC,
	 * and since L1 didn't enable virtual interrupt masking,
	 * L2 should receive it and not L1.
	 *
	 * L2 also has virtual interrupt 'VINTR_IRQ_NUMBER' pending in V_IRQ
	 * so it should also receive it after the following 'sti'.
	 */
	x2apic_write_reg(APIC_ICR,
		APIC_DEST_SELF | APIC_INT_ASSERT | INTR_IRQ_NUMBER);

	__asm__ __volatile__(
		"sti\n"
		"nop\n"
	);

	GUEST_ASSERT(vintr_irq_called);
	GUEST_ASSERT(intr_irq_called);

	__asm__ __volatile__(
		"vmcall\n"
	);
}

static void l1_guest_code(struct svm_test_data *svm)
{
	#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	struct vmcb *vmcb = svm->vmcb;

	x2apic_enable();

	/* Prepare for L2 execution. */
	generic_svm_setup(svm, l2_guest_code,
			  &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	/* No virtual interrupt masking */
	vmcb->control.int_ctl &= ~V_INTR_MASKING_MASK;

	/* No intercepts for real and virtual interrupts */
	vmcb->control.intercept &= ~(BIT(INTERCEPT_INTR) | BIT(INTERCEPT_VINTR));

	/* Make a virtual interrupt VINTR_IRQ_NUMBER pending */
	vmcb->control.int_ctl |= V_IRQ_MASK | (0x1 << V_INTR_PRIO_SHIFT);
	vmcb->control.int_vector = VINTR_IRQ_NUMBER;

	run_guest(vmcb, svm->vmcb_gpa);
	GUEST_ASSERT(vmcb->control.exit_code == SVM_EXIT_VMMCALL);
	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	vm_vaddr_t svm_gva;

	nested_svm_check_supported();

	vm = vm_create_default(VCPU_ID, 0, (void *) l1_guest_code);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);

	vm_install_exception_handler(vm, VINTR_IRQ_NUMBER, vintr_irq_handler);
	vm_install_exception_handler(vm, INTR_IRQ_NUMBER, intr_irq_handler);

	vcpu_alloc_svm(vm, &svm_gva);
	vcpu_args_set(vm, VCPU_ID, 1, svm_gva);

	struct kvm_run *run = vcpu_state(vm, VCPU_ID);
	struct ucall uc;

	vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Got exit_reason other than KVM_EXIT_IO: %u (%s)\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));

	switch (get_ucall(vm, VCPU_ID, &uc)) {
	case UCALL_ABORT:
		TEST_FAIL("%s", (const char *)uc.args[0]);
		break;
		/* NOT REACHED */
	case UCALL_DONE:
		goto done;
	default:
		TEST_FAIL("Unknown ucall 0x%lx.", uc.cmd);
	}
done:
	kvm_vm_free(vm);
	return 0;
}
