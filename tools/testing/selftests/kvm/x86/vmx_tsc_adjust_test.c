// SPDX-License-Identifier: GPL-2.0-only
/*
 * vmx_tsc_adjust_test
 *
 * Copyright (C) 2018, Google LLC.
 *
 * IA32_TSC_ADJUST test
 *
 * According to the SDM, "if an execution of WRMSR to the
 * IA32_TIME_STAMP_COUNTER MSR adds (or subtracts) value X from the TSC,
 * the logical processor also adds (or subtracts) value X from the
 * IA32_TSC_ADJUST MSR.
 *
 * Note that when L1 doesn't intercept writes to IA32_TSC, a
 * WRMSR(IA32_TSC) from L2 sets L1's TSC value, not L2's perceived TSC
 * value.
 *
 * This test verifies that this unusual case is handled correctly.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#include <string.h>
#include <sys/ioctl.h>

#include "kselftest.h"

#ifndef MSR_IA32_TSC_ADJUST
#define MSR_IA32_TSC_ADJUST 0x3b
#endif

#define TSC_ADJUST_VALUE (1ll << 32)
#define TSC_OFFSET_VALUE -(1ll << 48)

enum {
	PORT_ABORT = 0x1000,
	PORT_REPORT,
	PORT_DONE,
};

enum {
	VMXON_PAGE = 0,
	VMCS_PAGE,
	MSR_BITMAP_PAGE,

	NUM_VMX_PAGES,
};

/* The virtual machine object. */
static struct kvm_vm *vm;

static void check_ia32_tsc_adjust(int64_t max)
{
	int64_t adjust;

	adjust = rdmsr(MSR_IA32_TSC_ADJUST);
	GUEST_SYNC(adjust);
	GUEST_ASSERT(adjust <= max);
}

static void l2_guest_code(void)
{
	uint64_t l1_tsc = rdtsc() - TSC_OFFSET_VALUE;

	wrmsr(MSR_IA32_TSC, l1_tsc - TSC_ADJUST_VALUE);
	check_ia32_tsc_adjust(-2 * TSC_ADJUST_VALUE);

	/* Exit to L1 */
	__asm__ __volatile__("vmcall");
}

static void l1_guest_code(struct vmx_pages *vmx_pages)
{
#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	uint32_t control;
	uintptr_t save_cr3;

	GUEST_ASSERT(rdtsc() < TSC_ADJUST_VALUE);
	wrmsr(MSR_IA32_TSC, rdtsc() - TSC_ADJUST_VALUE);
	check_ia32_tsc_adjust(-1 * TSC_ADJUST_VALUE);

	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));

	/* Prepare the VMCS for L2 execution. */
	prepare_vmcs(vmx_pages, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);
	control = vmreadz(CPU_BASED_VM_EXEC_CONTROL);
	control |= CPU_BASED_USE_MSR_BITMAPS | CPU_BASED_USE_TSC_OFFSETTING;
	vmwrite(CPU_BASED_VM_EXEC_CONTROL, control);
	vmwrite(TSC_OFFSET, TSC_OFFSET_VALUE);

	/* Jump into L2.  First, test failure to load guest CR3.  */
	save_cr3 = vmreadz(GUEST_CR3);
	vmwrite(GUEST_CR3, -1ull);
	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) ==
		     (EXIT_REASON_FAILED_VMENTRY | EXIT_REASON_INVALID_STATE));
	check_ia32_tsc_adjust(-1 * TSC_ADJUST_VALUE);
	vmwrite(GUEST_CR3, save_cr3);

	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	check_ia32_tsc_adjust(-2 * TSC_ADJUST_VALUE);

	GUEST_DONE();
}

static void report(int64_t val)
{
	pr_info("IA32_TSC_ADJUST is %ld (%lld * TSC_ADJUST_VALUE + %lld).\n",
		val, val / TSC_ADJUST_VALUE, val % TSC_ADJUST_VALUE);
}

int main(int argc, char *argv[])
{
	vm_vaddr_t vmx_pages_gva;
	struct kvm_vcpu *vcpu;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));

	vm = vm_create_with_one_vcpu(&vcpu, (void *) l1_guest_code);

	/* Allocate VMX pages and shared descriptors (vmx_pages). */
	vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vcpu, 1, vmx_pages_gva);

	for (;;) {
		struct ucall uc;

		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		case UCALL_SYNC:
			report(uc.args[1]);
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	kvm_vm_free(vm);
	return 0;
}
