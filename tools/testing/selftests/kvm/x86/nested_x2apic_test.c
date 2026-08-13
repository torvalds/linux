// SPDX-License-Identifier: GPL-2.0-only
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"
#include "svm_util.h"

/*
 * Use the kernel's posted interrupt vectors to minimize the risk of crashing
 * the host if KVM is buggy.  Note, the vectors aren't set in stone, ideally
 * these will be kept up-to-date if the kernel vectors change, but it's "fine"
 * if they are stale.
 */
#define POSTED_INTR_VECTOR		0xf2
#define POSTED_INTR_WAKEUP_VECTOR	0xf1
#define POSTED_INTR_NESTED_VECTOR	0xf0

static bool inhibit_apicv;
static volatile unsigned int nr_irqs;

static void guest_irq_handler(struct ex_regs *regs)
{
	nr_irqs++;
	x2apic_write_reg(APIC_EOI, 0);
}

static void l2_guest_code(void)
{
	if (inhibit_apicv)
		wrmsr(MSR_IA32_APICBASE, rdmsr(MSR_IA32_APICBASE) & GENMASK_ULL(11, 0));
	asm volatile("cpuid" ::: "eax", "ebx", "ecx", "edx");
}

static void l1_svm_code(struct svm_test_data *svm)
{
	struct vmcb_control_area *ctrl = &svm->vmcb->control;

	generic_svm_setup(svm, l2_guest_code);
	ctrl->intercept |= BIT_ULL(INTERCEPT_CPUID) | BIT_ULL(INTERCEPT_MSR_PROT);

	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_ASSERT_EQ(ctrl->exit_code, SVM_EXIT_CPUID);

	stgi();
}

static void l1_vmx_code(struct vmx_pages *vmx)
{
	u64 control;

	GUEST_ASSERT_EQ(prepare_for_vmx_operation(vmx), true);
	GUEST_ASSERT_EQ(load_vmcs(vmx), true);

	prepare_vmcs(vmx, NULL);
	GUEST_ASSERT_EQ(vmwrite(GUEST_RIP, (unsigned long)l2_guest_code), 0);

	control = vmreadz(CPU_BASED_VM_EXEC_CONTROL);
	control |= CPU_BASED_USE_MSR_BITMAPS;
	GUEST_ASSERT_EQ(vmwrite(CPU_BASED_VM_EXEC_CONTROL, control), 0);

	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT_EQ(vmreadz(VM_EXIT_REASON), EXIT_REASON_CPUID);
}

static void l1_guest_code(void *test_data)
{
	x2apic_enable();

	if (this_cpu_has(X86_FEATURE_SVM))
		l1_svm_code(test_data);
	else
		l1_vmx_code(test_data);

	sti_nop();

	x2apic_write_reg(APIC_ICR, APIC_DEST_SELF | APIC_INT_ASSERT | POSTED_INTR_VECTOR);
	x2apic_write_reg(APIC_ICR, APIC_DEST_SELF | APIC_INT_ASSERT | POSTED_INTR_WAKEUP_VECTOR);
	x2apic_write_reg(APIC_ICR, APIC_DEST_SELF | APIC_INT_ASSERT | POSTED_INTR_NESTED_VECTOR);
	GUEST_ASSERT_EQ(nr_irqs, 3);
	GUEST_DONE();
}

static void __test_x2apic_intercepts(void)
{
	gva_t nested_test_data_gva;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	vm_install_exception_handler(vm, POSTED_INTR_VECTOR, guest_irq_handler);
	vm_install_exception_handler(vm, POSTED_INTR_WAKEUP_VECTOR, guest_irq_handler);
	vm_install_exception_handler(vm, POSTED_INTR_NESTED_VECTOR, guest_irq_handler);

	sync_global_to_guest(vm, inhibit_apicv);

	if (kvm_cpu_has(X86_FEATURE_SVM))
		vcpu_alloc_svm(vm, &nested_test_data_gva);
	else
		vcpu_alloc_vmx(vm, &nested_test_data_gva);

	vcpu_args_set(vcpu, 1, nested_test_data_gva);

	vcpu_run(vcpu);

	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_DONE:
		break;
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
		break;
	default:
		TEST_FAIL("Expected DONE, got unexpected ucall %lu", uc.cmd);
	}

	kvm_vm_free(vm);
}

#define test_x2apic_intercepts(inhibit_apic_setting)	\
do {							\
	inhibit_apic_setting;				\
							\
	__test_x2apic_intercepts();			\
} while (0)

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM) || kvm_cpu_has(X86_FEATURE_VMX));

	test_x2apic_intercepts(inhibit_apicv = true);
	test_x2apic_intercepts(inhibit_apicv = false);
}
