// SPDX-License-Identifier: GPL-2.0-only
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#define GOOD_IPI_VECTOR 0xe0
#define BAD_IPI_VECTOR 0xf0

static volatile int good_ipis_received;

static void good_ipi_handler(struct ex_regs *regs)
{
	good_ipis_received++;
}

static void bad_ipi_handler(struct ex_regs *regs)
{
	GUEST_FAIL("Received \"bad\" IPI; ICR MMIO write should have been ignored");
}

static void l2_guest_code(void)
{
	x2apic_enable();
	vmcall();

	xapic_enable();
	xapic_write_reg(APIC_ID, 1 << 24);
	vmcall();
}

static void l1_guest_code(struct vmx_pages *vmx_pages)
{
#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	uint32_t control;

	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));

	/* Prepare the VMCS for L2 execution. */
	prepare_vmcs(vmx_pages, l2_guest_code, &l2_guest_stack[L2_GUEST_STACK_SIZE]);
	control = vmreadz(CPU_BASED_VM_EXEC_CONTROL);
	control |= CPU_BASED_USE_MSR_BITMAPS;
	vmwrite(CPU_BASED_VM_EXEC_CONTROL, control);

	/* Modify APIC ID to coerce KVM into inhibiting APICv. */
	xapic_enable();
	xapic_write_reg(APIC_ID, 1 << 24);

	/*
	 * Generate+receive an IRQ without doing EOI to get an IRQ set in vISR
	 * but not SVI.  APICv should be inhibited due to running with a
	 * modified APIC ID.
	 */
	xapic_write_reg(APIC_ICR, APIC_DEST_SELF | APIC_DM_FIXED | GOOD_IPI_VECTOR);
	GUEST_ASSERT_EQ(xapic_read_reg(APIC_ID), 1 << 24);

	/* Enable IRQs and verify the IRQ was received. */
	sti_nop();
	GUEST_ASSERT_EQ(good_ipis_received, 1);

	/*
	 * Run L2 to switch to x2APIC mode, which in turn will uninhibit APICv,
	 * as KVM should force the APIC ID back to its default.
	 */
	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);
	vmwrite(GUEST_RIP, vmreadz(GUEST_RIP) + vmreadz(VM_EXIT_INSTRUCTION_LEN));
	GUEST_ASSERT(rdmsr(MSR_IA32_APICBASE) & MSR_IA32_APICBASE_EXTD);

	/*
	 * Scribble the APIC access page to verify KVM disabled xAPIC
	 * virtualization in vmcs01, and to verify that KVM flushes L1's TLB
	 * when L2 switches back to accelerated xAPIC mode.
	 */
	xapic_write_reg(APIC_ICR2, 0xdeadbeefu);
	xapic_write_reg(APIC_ICR, APIC_DEST_SELF | APIC_DM_FIXED | BAD_IPI_VECTOR);

	/*
	 * Verify the IRQ is still in-service and emit an EOI to verify KVM
	 * propagates the highest vISR vector to SVI when APICv is activated
	 * (and does so even if APICv was uninhibited while L2 was active).
	 */
	GUEST_ASSERT_EQ(x2apic_read_reg(APIC_ISR + APIC_VECTOR_TO_REG_OFFSET(GOOD_IPI_VECTOR)),
			BIT(APIC_VECTOR_TO_BIT_NUMBER(GOOD_IPI_VECTOR)));
	x2apic_write_reg(APIC_EOI, 0);
	GUEST_ASSERT_EQ(x2apic_read_reg(APIC_ISR + APIC_VECTOR_TO_REG_OFFSET(GOOD_IPI_VECTOR)), 0);

	/*
	 * Run L2 one more time to switch back to xAPIC mode to verify that KVM
	 * handles the x2APIC => xAPIC transition and inhibits APICv while L2
	 * is active.
	 */
	GUEST_ASSERT(!vmresume());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);
	GUEST_ASSERT(!(rdmsr(MSR_IA32_APICBASE) & MSR_IA32_APICBASE_EXTD));

	xapic_write_reg(APIC_ICR, APIC_DEST_SELF | APIC_DM_FIXED | GOOD_IPI_VECTOR);
	/* Re-enable IRQs, as VM-Exit clears RFLAGS.IF. */
	sti_nop();
	GUEST_ASSERT_EQ(good_ipis_received, 2);

	GUEST_ASSERT_EQ(xapic_read_reg(APIC_ISR + APIC_VECTOR_TO_REG_OFFSET(GOOD_IPI_VECTOR)),
			BIT(APIC_VECTOR_TO_BIT_NUMBER(GOOD_IPI_VECTOR)));
	xapic_write_reg(APIC_EOI, 0);
	GUEST_ASSERT_EQ(xapic_read_reg(APIC_ISR + APIC_VECTOR_TO_REG_OFFSET(GOOD_IPI_VECTOR)), 0);
	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	vm_vaddr_t vmx_pages_gva;
	struct vmx_pages *vmx;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);

	vmx = vcpu_alloc_vmx(vm, &vmx_pages_gva);
	prepare_virtualize_apic_accesses(vmx, vm);
	vcpu_args_set(vcpu, 1, vmx_pages_gva);

	virt_pg_map(vm, APIC_DEFAULT_GPA, APIC_DEFAULT_GPA);
	vm_install_exception_handler(vm, BAD_IPI_VECTOR, bad_ipi_handler);
	vm_install_exception_handler(vm, GOOD_IPI_VECTOR, good_ipi_handler);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
		/* NOT REACHED */
	case UCALL_DONE:
		break;
	default:
		TEST_FAIL("Unexpected ucall %lu", uc.cmd);
	}

	/*
	 * Verify at least two IRQs were injected.  Unfortunately, KVM counts
	 * re-injected IRQs (e.g. if delivering the IRQ hits an EPT violation),
	 * so being more precise isn't possible given the current stats.
	 */
	TEST_ASSERT(vcpu_get_stat(vcpu, irq_injections) >= 2,
		    "Wanted at least 2 IRQ injections, got %lu\n",
		    vcpu_get_stat(vcpu, irq_injections));

	kvm_vm_free(vm);
	return 0;
}
