// SPDX-License-Identifier: GPL-2.0-only
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#include <string.h>
#include <sys/ioctl.h>

#include "kselftest.h"

#define ARBITRARY_IO_PORT 0x80

static void l2_guest_code(void)
{
	/*
	 * Generate an exit to L0 userspace, i.e. main(), via I/O to an
	 * arbitrary port.
	 */
	asm volatile("inb $" __stringify(ARBITRARY_IO_PORT) ", %%al"
		     ::: "rax");
	GUEST_FAIL("L2 resumed after stuffing invalid guest state");
}

static void l1_guest_code(struct vmx_pages *vmx_pages)
{
	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));

	/* Prepare the VMCS for L2 execution. */
	prepare_vmcs(vmx_pages, l2_guest_code);

	/*
	 * L2 must be run without unrestricted guest, verify that the selftests
	 * library hasn't enabled it.  Because KVM selftests jump directly to
	 * 64-bit mode, unrestricted guest support isn't required.
	 */
	GUEST_ASSERT(!(vmreadz(CPU_BASED_VM_EXEC_CONTROL) & CPU_BASED_ACTIVATE_SECONDARY_CONTROLS) ||
		     !(vmreadz(SECONDARY_VM_EXEC_CONTROL) & SECONDARY_EXEC_UNRESTRICTED_GUEST));

	GUEST_ASSERT(!vmlaunch());

	/* L2 should triple fault after main() stuffs invalid guest state. */
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_TRIPLE_FAULT);
	GUEST_DONE();
}

static void vcpu_run_to_io(struct kvm_vcpu *vcpu, bool want_l2)
{
	struct kvm_run *run = vcpu->run;

	vcpu_run(vcpu);

	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

	TEST_ASSERT(run->io.port == ARBITRARY_IO_PORT &&
		    (!!(run->flags & KVM_RUN_X86_GUEST_MODE) == want_l2  ||
		     !kvm_has_cap(KVM_CAP_X86_GUEST_MODE)),
		    "Expected IN from port 0x%x from L%u, got port 0x%x from L%u",
		    ARBITRARY_IO_PORT, 1 + want_l2, run->io.port,
		    1 + !!(run->flags & KVM_RUN_X86_GUEST_MODE));
}

static struct kvm_vm *vm_create_and_run_l2(struct kvm_vcpu **vcpu)
{
	gva_t vmx_pages_gva;
	struct kvm_vm *vm;

	vm = vm_create_with_one_vcpu(vcpu, l1_guest_code);

	/* Allocate VMX pages and shared descriptors (vmx_pages). */
	vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(*vcpu, 1, vmx_pages_gva);

	/*
	 * The first exit to L0 userspace should be an I/O access from L2.
	 * Running L1 should launch L2 without triggering an exit to userspace.
	 */
	vcpu_run_to_io(*vcpu, true);

	return vm;
}

static void test_invalid_l2_guest_state(void)
{
	struct kvm_sregs sregs;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	vm = vm_create_and_run_l2(&vcpu);

	/*
	 * Stuff invalid guest state for L2 by making TR unusuable.  The next
	 * KVM_RUN should induce a TRIPLE_FAULT in L2 as KVM doesn't support
	 * emulating invalid guest state for L2.
	 */
	memset(&sregs, 0, sizeof(sregs));
	vcpu_sregs_get(vcpu, &sregs);
	sregs.tr.unusable = 1;
	vcpu_sregs_set(vcpu, &sregs);

	vcpu_run(vcpu);

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_DONE:
		break;
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
	default:
		TEST_FAIL("Unexpected ucall: %lu", uc.cmd);
	}

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));

	test_invalid_l2_guest_state();
}
