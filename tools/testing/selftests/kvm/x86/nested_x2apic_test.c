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

	for (;;) {
		x2apic_write_reg(APIC_TASKPRI, 0xf0);
		GUEST_ASSERT_EQ(x2apic_read_reg(APIC_TASKPRI), 0xf0);

		asm volatile("cpuid" ::: "eax", "ebx", "ecx", "edx");
	}
}

static void l1_svm_code(struct svm_test_data *svm)
{
	struct vmcb_control_area *ctrl = &svm->vmcb->control;

	generic_svm_setup(svm, l2_guest_code);
	ctrl->intercept |= BIT_ULL(INTERCEPT_CPUID) | BIT_ULL(INTERCEPT_MSR_PROT);

	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_ASSERT_EQ(ctrl->exit_code, SVM_EXIT_CPUID);

	stgi();
	x2apic_write_reg(APIC_TASKPRI, 0);
}

static void l1_vmx_code(struct vmx_pages *vmx, struct hyperv_test_pages *hv_pages)
{
	u64 control;

	if (hv_pages) {
		wrmsr(HV_X64_MSR_GUEST_OS_ID, HYPERV_LINUX_OS_ID);
		enable_vp_assist(hv_pages->vp_assist_gpa, hv_pages->vp_assist);
		evmcs_enable();
	}

	GUEST_ASSERT_EQ(prepare_for_vmx_operation(vmx), true);

	if (hv_pages) {
		GUEST_ASSERT(load_evmcs(hv_pages));
		current_evmcs->hv_enlightenments_control.msr_bitmap = 1;
	} else {
		GUEST_ASSERT(load_vmcs(vmx));
	}

	prepare_vmcs(vmx, NULL);
	GUEST_ASSERT_EQ(vmwrite(GUEST_RIP, (unsigned long)l2_guest_code), 0);

	control = vmreadz(PIN_BASED_VM_EXEC_CONTROL);
	control |= PIN_BASED_EXT_INTR_MASK;
	vmwrite(PIN_BASED_VM_EXEC_CONTROL, control);

	control = vmreadz(CPU_BASED_VM_EXEC_CONTROL);
	control |= CPU_BASED_USE_MSR_BITMAPS | CPU_BASED_TPR_SHADOW;
	GUEST_ASSERT_EQ(vmwrite(CPU_BASED_VM_EXEC_CONTROL, control), 0);

	control = vmreadz(SECONDARY_VM_EXEC_CONTROL);
	control |= SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
		   SECONDARY_EXEC_APIC_REGISTER_VIRT |
		   SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY;
	control &= (rdmsr(MSR_IA32_VMX_PROCBASED_CTLS2) >> 32);
	GUEST_ASSERT_EQ(vmwrite(SECONDARY_VM_EXEC_CONTROL, control), 0);

	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT_EQ(vmreadz(VM_EXIT_REASON), EXIT_REASON_CPUID);
	GUEST_ASSERT_EQ(vmwrite(GUEST_RIP,
			vmreadz(GUEST_RIP) + vmreadz(VM_EXIT_INSTRUCTION_LEN)), 0);
}

static void l1_vmx_code_part2(void)
{
	u64 control;

	control = vmreadz(CPU_BASED_VM_EXEC_CONTROL);
	control &= ~CPU_BASED_TPR_SHADOW;
	GUEST_ASSERT_EQ(vmwrite(CPU_BASED_VM_EXEC_CONTROL, control), 0);

	control = vmreadz(SECONDARY_VM_EXEC_CONTROL);
	control &= ~(SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
			SECONDARY_EXEC_APIC_REGISTER_VIRT |
			SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY);
	GUEST_ASSERT_EQ(vmwrite(SECONDARY_VM_EXEC_CONTROL, control), 0);

	GUEST_ASSERT(!vmresume());
	GUEST_ASSERT_EQ(vmreadz(VM_EXIT_REASON), EXIT_REASON_CPUID);
	GUEST_ASSERT_EQ(vmwrite(GUEST_RIP,
			vmreadz(GUEST_RIP) + vmreadz(VM_EXIT_INSTRUCTION_LEN)), 0);
}

static void l1_test_x2apic_intercepts(void)
{
	GUEST_ASSERT_EQ(nr_irqs, 0);

	sti_nop();

	GUEST_ASSERT_EQ(x2apic_read_reg(APIC_TASKPRI), 0xf0);

	x2apic_write_reg(APIC_ICR, APIC_DEST_SELF | APIC_INT_ASSERT | POSTED_INTR_VECTOR);
	GUEST_ASSERT_EQ(nr_irqs, 0);

	x2apic_write_reg(APIC_TASKPRI, 0xff);
	GUEST_ASSERT_EQ(x2apic_read_reg(APIC_TASKPRI), 0xff);
	x2apic_write_reg(APIC_ICR, APIC_DEST_SELF | APIC_INT_ASSERT | POSTED_INTR_WAKEUP_VECTOR);
	GUEST_ASSERT_EQ(nr_irqs, 0);

	x2apic_write_reg(APIC_TASKPRI, 0);
	GUEST_ASSERT_EQ(x2apic_read_reg(APIC_TASKPRI), 0);

	x2apic_write_reg(APIC_ICR, APIC_DEST_SELF | APIC_INT_ASSERT | POSTED_INTR_NESTED_VECTOR);
	GUEST_ASSERT_EQ(nr_irqs, 3);

	nr_irqs = 0;
}

static void l1_guest_code(void *test_data, void *hv_pages)
{
	x2apic_enable();

	if (this_cpu_has(X86_FEATURE_SVM))
		l1_svm_code(test_data);
	else
		l1_vmx_code(test_data, hv_pages);

	GUEST_ASSERT_EQ(x2apic_read_reg(APIC_TASKPRI), 0);
	x2apic_write_reg(APIC_TASKPRI, 0xf0);

	l1_test_x2apic_intercepts();

	if (this_cpu_has(X86_FEATURE_VMX)) {
		l1_vmx_code_part2();
		l1_test_x2apic_intercepts();
	}

	GUEST_DONE();
}

static void test_x2apic_intercepts(bool with_inhibit_apicv, bool use_evmcs)
{
	gva_t nested_test_data_gva, hv_pages_gva = 0;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	inhibit_apicv = with_inhibit_apicv;

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	vm_install_exception_handler(vm, POSTED_INTR_VECTOR, guest_irq_handler);
	vm_install_exception_handler(vm, POSTED_INTR_WAKEUP_VECTOR, guest_irq_handler);
	vm_install_exception_handler(vm, POSTED_INTR_NESTED_VECTOR, guest_irq_handler);

	sync_global_to_guest(vm, inhibit_apicv);

	if (kvm_cpu_has(X86_FEATURE_SVM))
		vcpu_alloc_svm(vm, &nested_test_data_gva);
	else
		vcpu_alloc_vmx(vm, &nested_test_data_gva);

	if (use_evmcs) {
		vcpu_set_hv_cpuid(vcpu);
		vcpu_enable_evmcs(vcpu);

		vcpu_alloc_hyperv_test_pages(vm, &hv_pages_gva);
	}

	vcpu_args_set(vcpu, 2, nested_test_data_gva, hv_pages_gva);

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

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM) || kvm_cpu_has(X86_FEATURE_VMX));

	test_x2apic_intercepts(true, false);
	test_x2apic_intercepts(false, false);

	if (kvm_has_cap(KVM_CAP_HYPERV_ENLIGHTENED_VMCS)) {
		test_x2apic_intercepts(true, true);
		test_x2apic_intercepts(false, true);
	}
}
